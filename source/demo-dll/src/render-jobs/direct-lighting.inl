namespace RenderJob::DirectLightingPass
{
	struct Desc
	{
		int directionalLightIndex;
		FShaderSurface* colorTarget;
		FShaderSurface* depthStencilTex;
		FShaderSurface* gbufferBaseColorTex;
		FShaderSurface* gbufferNormalsTex;
		FShaderSurface* gbufferMetallicRoughnessAoTex;
		FSystemBuffer* sceneConstantBuffer;
		FSystemBuffer* viewConstantBuffer;
		FConfig renderConfig;
		uint32_t resX;
		uint32_t resY;
	};

	Result Execute(Sync* jobSync, const Desc& passDesc)
	{
		size_t renderToken = jobSync->GetToken();
		size_t colorTargetTransitionToken = passDesc.colorTarget->m_resource->GetTransitionToken();
		size_t depthTexTransitionToken = passDesc.depthStencilTex->m_resource->GetTransitionToken();
		size_t gbufferBaseColorTransitionToken = passDesc.gbufferBaseColorTex->m_resource->GetTransitionToken();
		size_t gbufferNormalsTransitionToken = passDesc.gbufferNormalsTex->m_resource->GetTransitionToken();
		size_t gbufferMetallicRoughnessAoTransitionToken = passDesc.gbufferMetallicRoughnessAoTex->m_resource->GetTransitionToken();
		FCommandList* cmdList = RenderBackend12::FetchCommandlist(L"direct_lighting", D3D12_COMMAND_LIST_TYPE_DIRECT);

		Result passResult;
		passResult.m_syncObj = cmdList->GetSync();
		passResult.m_task = concurrency::create_task([=]
		{
			SCOPED_CPU_EVENT("direct_lighting", PIX_COLOR_DEFAULT);
			D3DCommandList_t* d3dCmdList = cmdList->m_d3dCmdList.get();
			SCOPED_COMMAND_LIST_EVENT(cmdList, "direct_lighting", 0);

			// Transitions
			passDesc.colorTarget->m_resource->Transition(cmdList, colorTargetTransitionToken, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			passDesc.depthStencilTex->m_resource->Transition(cmdList, depthTexTransitionToken, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			passDesc.gbufferBaseColorTex->m_resource->Transition(cmdList, gbufferBaseColorTransitionToken, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			passDesc.gbufferNormalsTex->m_resource->Transition(cmdList, gbufferNormalsTransitionToken, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			passDesc.gbufferMetallicRoughnessAoTex->m_resource->Transition(cmdList, gbufferMetallicRoughnessAoTransitionToken, 0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			// Descriptor Heaps
			D3DDescriptorHeap_t* descriptorHeaps[] = { RenderBackend12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) };
			d3dCmdList->SetDescriptorHeaps(1, descriptorHeaps);

			// Root Signature
			std::unique_ptr<FRootSignature> rootsig = RenderBackend12::FetchRootSignature(
				L"direct_lighting_rootsig",
				cmdList,
				FRootSignature::Desc{ L"lighting/direct-lighting.hlsl", L"rootsig", L"rootsig_1_1" });

			d3dCmdList->SetComputeRootSignature(rootsig->m_rootsig);

			IDxcBlob* csBlob = RenderBackend12::CacheShader({
			L"lighting/direct-lighting.hlsl",
			L"cs_main",
			PrintString(L"THREAD_GROUP_SIZE_X=16 THREAD_GROUP_SIZE_Y=16 LIGHTING_ONLY=%d", passDesc.renderConfig.Viewmode == (int)Viewmode::LightingOnly ? 1 : 0),
			L"cs_6_6" });

			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = rootsig->m_rootsig;
			psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
			psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
			psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

			D3DPipelineState_t* pso = RenderBackend12::FetchComputePipelineState(psoDesc);
			d3dCmdList->SetPipelineState(pso);

			struct FPassConstants
			{
				uint32_t m_colorTargetUavIndex;
				uint32_t m_depthTargetSrvIndex;
				uint32_t m_gbufferBaseColorSrvIndex;
				uint32_t m_gbufferNormalsSrvIndex;
				uint32_t m_gbufferMetallicRoughnessAoSrvIndex;
			};

			FPassConstants cb = {};
			cb.m_colorTargetUavIndex = passDesc.colorTarget->m_descriptorIndices.UAVs[0];
			cb.m_depthTargetSrvIndex = passDesc.depthStencilTex->m_descriptorIndices.SRV;
			cb.m_gbufferBaseColorSrvIndex = passDesc.gbufferBaseColorTex->m_descriptorIndices.SRV;
			cb.m_gbufferNormalsSrvIndex = passDesc.gbufferNormalsTex->m_descriptorIndices.SRV;
			cb.m_gbufferMetallicRoughnessAoSrvIndex = passDesc.gbufferMetallicRoughnessAoTex->m_descriptorIndices.SRV;

			d3dCmdList->SetComputeRoot32BitConstants(0, sizeof(FPassConstants) / 4, &cb, 0);
			d3dCmdList->SetComputeRootConstantBufferView(1, passDesc.viewConstantBuffer->m_resource->m_d3dResource->GetGPUVirtualAddress());
			d3dCmdList->SetComputeRootConstantBufferView(2, passDesc.sceneConstantBuffer->m_resource->m_d3dResource->GetGPUVirtualAddress());

			// Dispatch
			const size_t threadGroupCountX = GetDispatchSize(passDesc.resX, 16);
			const size_t threadGroupCountY = GetDispatchSize(passDesc.resY, 16);
			d3dCmdList->Dispatch(threadGroupCountX, threadGroupCountY, 1);

			return cmdList;

		}).then([=](FCommandList* recordedCl) mutable
		{
			jobSync->Execute(renderToken, recordedCl);
		});

		return passResult;
	}
}