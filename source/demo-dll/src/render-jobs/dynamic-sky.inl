namespace RenderJob
{
	struct DynamicSkyPassDesc
	{
		FShaderSurface* colorTarget;
		FShaderSurface* depthStencilTarget;
		DXGI_FORMAT format;
		uint32_t resX;
		uint32_t resY;
		const FScene* scene;
		const FView* view;
		Vector2 jitter;
		FConfig renderConfig;
	};

	Result DynamicSkyPass(RenderJob::Sync* jobSync, const DynamicSkyPassDesc& passDesc)
	{
		size_t renderToken = jobSync->GetToken();
		size_t colorTargetTransitionToken = passDesc.colorTarget->m_resource->GetTransitionToken();
		size_t depthStencilTransitionToken = passDesc.depthStencilTarget->m_resource->GetTransitionToken();
		FCommandList* cmdList = RenderBackend12::FetchCommandlist(L"dynamicsky_pass_job", D3D12_COMMAND_LIST_TYPE_DIRECT);

		Result passResult;
		passResult.m_syncObj = cmdList->GetSync();
		passResult.m_task = concurrency::create_task([=]
		{
			SCOPED_CPU_EVENT("record_dynamicsky_pass", PIX_COLOR_DEFAULT);
			D3DCommandList_t* d3dCmdList = cmdList->m_d3dCmdList.get();
			SCOPED_COMMAND_LIST_EVENT(cmdList, "dynamicsky_pass", 0);

			passDesc.colorTarget->m_resource->Transition(cmdList, colorTargetTransitionToken, 0, D3D12_RESOURCE_STATE_RENDER_TARGET);
			passDesc.depthStencilTarget->m_resource->Transition(cmdList, depthStencilTransitionToken, 0, D3D12_RESOURCE_STATE_DEPTH_READ);

			// Descriptor Heaps
			D3DDescriptorHeap_t* descriptorHeaps[] = { RenderBackend12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) };
			d3dCmdList->SetDescriptorHeaps(1, descriptorHeaps);

			// Root Signature
			std::unique_ptr<FRootSignature> rootsig = RenderBackend12::FetchRootSignature(
				L"dynamicsky_rootsig",
				cmdList,
				FRootSignature::Desc{ L"environment-sky/dynamic-sky.hlsl", L"rootsig", L"rootsig_1_1" });
			d3dCmdList->SetGraphicsRootSignature(rootsig->m_rootsig);

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

				IDxcBlob* vsBlob = RenderBackend12::CacheShader({ L"environment-sky/dynamic-sky.hlsl", L"vs_main", L"" , L"vs_6_6" });
				IDxcBlob* psBlob = RenderBackend12::CacheShader({ L"environment-sky/dynamic-sky.hlsl", L"ps_main", L"" , L"ps_6_6" });

				vs.pShaderBytecode = vsBlob->GetBufferPointer();
				vs.BytecodeLength = vsBlob->GetBufferSize();
				ps.pShaderBytecode = psBlob->GetBufferPointer();
				ps.BytecodeLength = psBlob->GetBufferSize();
			}

			// PSO - Rasterizer State
			{
				D3D12_RASTERIZER_DESC& desc = psoDesc.RasterizerState;
				desc.FillMode = D3D12_FILL_MODE_SOLID;
				desc.CullMode = D3D12_CULL_MODE_BACK;
				desc.FrontCounterClockwise = FALSE;
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
				desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
				desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
				desc.StencilEnable = FALSE;
			}

			D3DPipelineState_t* pso = RenderBackend12::FetchGraphicsPipelineState(psoDesc);
			d3dCmdList->SetPipelineState(pso);

			D3D12_VIEWPORT viewport{ 0.f, 0.f, (float)passDesc.resX, (float)passDesc.resY, 0.f, 1.f };
			D3D12_RECT screenRect{ 0, 0, (LONG)passDesc.resX, (LONG)passDesc.resY };
			d3dCmdList->RSSetViewports(1, &viewport);
			d3dCmdList->RSSetScissorRects(1, &screenRect);

			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { RenderBackend12::GetCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, passDesc.colorTarget->m_descriptorIndices.RTVorDSVs[0]) };
			D3D12_CPU_DESCRIPTOR_HANDLE dsv = RenderBackend12::GetCPUDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, passDesc.depthStencilTarget->m_descriptorIndices.RTVorDSVs[0]);
			d3dCmdList->OMSetRenderTargets(1, rtvs, FALSE, &dsv);

			d3dCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			FPerezDistribution perezConstants;
			const float t = passDesc.renderConfig.Turbidity;
			perezConstants.A = Vector4(0.1787 * t - 1.4630, -0.0193 * t - 0.2592, -0.0167 * t - 0.2608, 0.f);
			perezConstants.B = Vector4(-0.3554 * t + 0.4275, -0.0665 * t + 0.0008, -0.0950 * t + 0.0092, 0.f);
			perezConstants.C = Vector4(-0.0227 * t + 5.3251, -0.0004 * t + 0.2125, -0.0079 * t + 0.2102, 0.f);
			perezConstants.D = Vector4(0.1206 * t - 2.5771, -0.0641 * t - 0.8989, -0.0441 * t - 1.6537, 0.f);
			perezConstants.E = Vector4(-0.0670 * t + 0.3703, -0.0033 * t + 0.0452, -0.0109 * t + 0.0529, 0.f);

			// Constant buffer
			struct Constants
			{
				Matrix invParallaxViewProjMatrix;
				FPerezDistribution perez;
				float turbidity;
				Vector3 sunDir;
			};

			std::unique_ptr<FSystemBuffer> cbuf{ RenderBackend12::CreateNewSystemBuffer(
				L"dynamic_sky_cb",
				FResource::AccessMode::CpuWriteOnly,
				sizeof(Constants),
				cmdList->GetFence(FCommandList::Sync::GpuFinish),
				[passDesc, perezConstants](uint8_t* pDest)
				{
					Matrix parallaxViewMatrix = passDesc.view->m_viewTransform;
					parallaxViewMatrix.Translation(Vector3::Zero);

					// Sun direction
					Vector3 L = passDesc.scene->m_sunDir;
					L.Normalize();

					auto cb = reinterpret_cast<Constants*>(pDest);
					cb->invParallaxViewProjMatrix = (parallaxViewMatrix * passDesc.view->m_projectionTransform).Invert();
					cb->perez = perezConstants;
					cb->turbidity = passDesc.renderConfig.Turbidity;
					cb->sunDir = L;

				}) };

			d3dCmdList->SetGraphicsRootConstantBufferView(0, cbuf->m_resource->m_d3dResource->GetGPUVirtualAddress());
			d3dCmdList->DrawInstanced(3, 1, 0, 0);

			return cmdList;

		}).then([=](FCommandList* recordedCl) mutable
		{
			jobSync->Execute(renderToken, recordedCl);
		});

		return passResult;
	}
}