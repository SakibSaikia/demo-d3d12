namespace RenderJob::ForwardLightingPass
{
	struct Desc
	{
		FShaderSurface* colorTarget;
		FShaderSurface* depthStencilTarget;
		FSystemBuffer* sceneConstantBuffer;
		FSystemBuffer* viewConstantBuffer;
		DXGI_FORMAT format;
		uint32_t resX;
		uint32_t resY;
		const FScene* scene;
		FConfig renderConfig;
	};

	Result Execute(Sync* jobSync, const Desc& passDesc)
	{
		size_t renderToken = jobSync->GetToken();
		size_t colorTargetTransitionToken = passDesc.colorTarget->m_resource->GetTransitionToken();
		size_t depthStencilTransitionToken = passDesc.depthStencilTarget->m_resource->GetTransitionToken();
		FCommandList* cmdList = RenderBackend12::FetchCommandlist(L"forward_pass_job", D3D12_COMMAND_LIST_TYPE_DIRECT);

		Result passResult;
		passResult.m_syncObj = cmdList->GetSync();
		passResult.m_task = concurrency::create_task([=]
		{
			SCOPED_CPU_EVENT("record_forward_pass", PIX_COLOR_DEFAULT);
			D3DCommandList_t* d3dCmdList = cmdList->m_d3dCmdList.get();
			SCOPED_COMMAND_LIST_EVENT(cmdList, "forward_pass", 0);

			passDesc.colorTarget->m_resource->Transition(cmdList, colorTargetTransitionToken, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
			passDesc.depthStencilTarget->m_resource->Transition(cmdList, depthStencilTransitionToken, 0, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			// Descriptor heaps need to be set before setting the root signature when using HLSL Dynamic Resources
			// https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html
			D3DDescriptorHeap_t* descriptorHeaps[] =
			{
				RenderBackend12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
				RenderBackend12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			};
			d3dCmdList->SetDescriptorHeaps(2, descriptorHeaps);

			// Root Signature
			std::unique_ptr<FRootSignature> rootsig = RenderBackend12::FetchRootSignature(
				L"forward_pass_rootsig",
				cmdList,
				FRootSignature::Desc{ L"geo-raster/forward-pass.hlsl", L"rootsig", L"rootsig_1_1" });
			d3dCmdList->SetGraphicsRootSignature(rootsig->m_rootsig);

			d3dCmdList->SetGraphicsRootConstantBufferView(1, passDesc.viewConstantBuffer->m_resource->m_d3dResource->GetGPUVirtualAddress());
			d3dCmdList->SetGraphicsRootConstantBufferView(2, passDesc.sceneConstantBuffer->m_resource->m_d3dResource->GetGPUVirtualAddress());

			D3D12_VIEWPORT viewport{ 0.f, 0.f, (float)passDesc.resX, (float)passDesc.resY, 0.f, 1.f };
			D3D12_RECT screenRect{ 0, 0, (LONG)passDesc.resX, (LONG)passDesc.resY };
			d3dCmdList->RSSetViewports(1, &viewport);
			d3dCmdList->RSSetScissorRects(1, &screenRect);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { RenderBackend12::GetCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, passDesc.colorTarget->m_descriptorIndices.RTVorDSVs[0]) };
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = RenderBackend12::GetCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, passDesc.depthStencilTarget->m_descriptorIndices.RTVorDSVs[0]);
			d3dCmdList->OMSetRenderTargets(1, rtvs, FALSE, &dsv);

			float clearColor[] = { .8f, .8f, 1.f, 0.f };
			d3dCmdList->ClearRenderTargetView(rtvs[0], clearColor, 0, nullptr);
			d3dCmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.f, 0, 0, nullptr);

			// Issue scene draws
			int count = 0;
			for (int meshIndex = 0; meshIndex < passDesc.scene->m_sceneMeshes.GetCount(); ++meshIndex)
			{
				const bool bHidden = passDesc.scene->m_sceneMeshes.m_visibleList[meshIndex] == 0;
				if (bHidden)
				{
					continue;
				}

				const FMesh& mesh = passDesc.scene->m_sceneMeshes.m_entityList[meshIndex];
				SCOPED_COMMAND_LIST_EVENT(cmdList, passDesc.scene->m_sceneMeshes.m_entityNames[meshIndex].c_str(), 0);

				for (const FMeshPrimitive& primitive : mesh.m_primitives)
				{
					d3dCmdList->IASetPrimitiveTopology(primitive.m_topology);

					// PSO
					D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
					psoDesc.NodeMask = 1;
					psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
					psoDesc.pRootSignature = rootsig->m_rootsig;
					psoDesc.SampleMask = UINT_MAX;
					psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
					psoDesc.NumRenderTargets = 1;
					psoDesc.RTVFormats[0] = passDesc.format;
					psoDesc.SampleDesc.Count = 1;
					psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

					// PSO - Shaders
					{
						D3D12_SHADER_BYTECODE& vs = psoDesc.VS;
						D3D12_SHADER_BYTECODE& ps = psoDesc.PS;

						std::wstring shaderMacros = PrintString(
							L"VIEWMODE=%d  DIRECT_LIGHTING=%d DIFFUSE_IBL=%d SPECULAR_IBL=%d",
							passDesc.renderConfig.Viewmode,
							passDesc.renderConfig.EnableDirectLighting ? 1 : 0,
							passDesc.renderConfig.EnableDiffuseIBL ? 1 : 0,
							passDesc.renderConfig.EnableSpecularIBL ? 1 : 0);

						IDxcBlob* vsBlob = RenderBackend12::CacheShader({ L"geo-raster/forward-pass.hlsl", L"vs_main", L"" , L"vs_6_6" });
						IDxcBlob* psBlob = RenderBackend12::CacheShader({ L"geo-raster/forward-pass.hlsl", L"ps_main", shaderMacros , L"ps_6_6" });

						vs.pShaderBytecode = vsBlob->GetBufferPointer();
						vs.BytecodeLength = vsBlob->GetBufferSize();
						ps.pShaderBytecode = psBlob->GetBufferPointer();
						ps.BytecodeLength = psBlob->GetBufferSize();
					}

					// PSO - Rasterizer State
					{
						bool bDoubleSidedMaterial = passDesc.scene->m_materialList[primitive.m_materialIndex].m_doubleSided;

						D3D12_RASTERIZER_DESC& desc = psoDesc.RasterizerState;
						desc.FillMode = D3D12_FILL_MODE_SOLID;
						desc.CullMode = bDoubleSidedMaterial ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
						desc.FrontCounterClockwise = TRUE;
						desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
						desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
						desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
						desc.DepthClipEnable = TRUE;
						desc.MultisampleEnable = FALSE;
						desc.AntialiasedLineEnable = FALSE;
						desc.ForcedSampleCount = 0;
						desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
					}

					// PSO - Blend State
					{
						D3D12_BLEND_DESC& desc = psoDesc.BlendState;
						desc.AlphaToCoverageEnable = FALSE;
						desc.IndependentBlendEnable = FALSE;
						desc.RenderTarget[0].BlendEnable = FALSE;
						desc.RenderTarget[0].LogicOpEnable = FALSE;
						desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
					}

					// PSO - Depth Stencil State
					{
						D3D12_DEPTH_STENCIL_DESC& desc = psoDesc.DepthStencilState;
						desc.DepthEnable = TRUE;
						desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
						desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
						desc.StencilEnable = FALSE;
					}

					D3DPipelineState_t* pso = RenderBackend12::FetchGraphicsPipelineState(psoDesc);
					d3dCmdList->SetPipelineState(pso);

					// Geometry constants
					struct PrimitiveCbLayout
					{
						Matrix localToWorldTransform;
						int m_indexAccessor;
						int m_positionAccessor;
						int m_uvAccessor;
						int m_normalAccessor;
						int m_tangentAccessor;
						int m_materialIndex;
					} primCb =
					{
						passDesc.scene->m_sceneMeshes.m_transformList[meshIndex],
						primitive.m_indexAccessor,
						primitive.m_positionAccessor,
						primitive.m_uvAccessor,
						primitive.m_normalAccessor,
						primitive.m_tangentAccessor,
						primitive.m_materialIndex
					};

					d3dCmdList->SetGraphicsRoot32BitConstants(0, sizeof(PrimitiveCbLayout) / 4, &primCb, 0);

					d3dCmdList->DrawInstanced(primitive.m_indexCount, 1, 0, 0);
					count++;
				}
			}

			return cmdList;

		}).then([=](FCommandList* recordedCl) mutable
		{
			jobSync->Execute(renderToken, recordedCl);
		});

		return passResult;
	}
}