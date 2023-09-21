#include "CustomMeshPassRendering/RenderQueue.h"

//#include "CoreMinimal.h"

//#include "Materials/Material.h"
#include "MeshPassProcessor.inl"
#include "RenderGraphUtils.h"
#include "SimpleMeshDrawCommandPass.h"

#include "Shader.h"



DEFINE_LOG_CATEGORY(LogCustomMeshPass)

DECLARE_GPU_STAT_NAMED(CustomMeshPassDraw,TEXT("CustomMeshPass Draw"))
DECLARE_GPU_STAT_NAMED(CustomMeshPassCopy, TEXT("CustomMeshPass Copy"))

DECLARE_STATS_GROUP(TEXT("CustomMeshPass"),STATGROUP_CustomMeshPass,STATCAT_ADVANCED)
DECLARE_CYCLE_STAT(TEXT("CustomMeshPass Tick"),STAT_CustomMeshPass_Tick, STATGROUP_CustomMeshPass)


BEGIN_SHADER_PARAMETER_STRUCT(FCustomMeshPassPassParameters, )
SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()







void Render(FRDGBuilder& GraphBuilder, FSceneInterface* Scene, const FSceneView& View,FIntPoint Size,
	TSet<FPrimitiveSceneProxy*>& Proxies, FTextureRHIRef RenderTarget,FTextureRHIRef DepthTarget,
	FTextureRHIRef ResolveTexture
	)
{
	struct FMeshInfo
	{
		FMeshBatch MeshBatch;
		uint64 ElementMask = ~0ull;
		FPrimitiveSceneProxy* Proxy;
	};


	

	TArray<FMeshInfo> MeshInfos;


	/// collect mesh infos
	{
		TArray<FMeshBatch> MeshBatches;

		const int32 Lod = 0;

		for (auto Proxy : Proxies)
		{
			Proxy->GetMeshDescription(Lod, MeshBatches);

			if (MeshBatches.Num() > 0)
			{
				auto & MeshInfo =  MeshInfos.AddDefaulted_GetRef();
				MeshInfo.MeshBatch = MeshBatches[0];
				MeshInfo.ElementMask = 1<<0;
				MeshInfo.Proxy = Proxy;
			}
		}

	}

	FRDGTextureRef RDGDepthTarget = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(DepthTarget, TEXT("CMPDepthTarget")));
	FRDGTextureRef RDGResolveTexture = NULL;
	FRDGTextureRef RDGRenderTarget = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget, TEXT("CMPRenderTarget")));

	if (ResolveTexture.IsValid())
	{
		RDGResolveTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(ResolveTexture, TEXT("CMPResolveTexture")));
	}
	

	{
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		{
			RDG_EVENT_SCOPE(GraphBuilder, "CustomMeshPassRender");
			RDG_GPU_STAT_SCOPE(GraphBuilder, CustomMeshPassDraw);
			

			FCustomMeshPassPassParameters* PassParameter = GraphBuilder.AllocParameters<FCustomMeshPassPassParameters>();
		
			PassParameter->RenderTargets[0] = FRenderTargetBinding(RDGRenderTarget, ERenderTargetLoadAction::EClear);
			PassParameter->RenderTargets.DepthStencil = FDepthStencilBinding(RDGDepthTarget, ERenderTargetLoadAction::EClear,
				ERenderTargetLoadAction::ENoAction, FExclusiveDepthStencil::Type::DepthWrite_StencilNop);

			PassParameter->View = View.ViewUniformBuffer;
			
			FIntRect ViewRect(0, 0, Size.X, Size.Y);
			
			AddSimpleMeshPass(GraphBuilder, PassParameter, Scene->GetRenderScene(), View, nullptr, RDG_EVENT_NAME("CustomMeshPassRender"), ViewRect,
			[MeshInfos = MoveTemp(MeshInfos),&View](FDynamicPassMeshDrawListContext* Context)
			{
				UE_LOG(LogCustomMeshPass,Log,TEXT("CustomMeshPass Pass:Mesh Batch Num = %d"), MeshInfos.Num());
				
				FCSMMeshPassProcessor MeshPassProcess(nullptr, &View, Context);
				for (auto& MeshInfo : MeshInfos)
				{
					auto& MeshBatch = MeshInfo.MeshBatch;
					if (MeshBatch.MaterialRenderProxy != NULL)
					{
						MeshBatch.MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View.GetFeatureLevel());
						MeshPassProcess.AddMeshBatch(MeshInfo.MeshBatch, MeshInfo.ElementMask, MeshInfo.Proxy);
					}

				}
			}
			);
			
			
			
			

			

		}

		

#if 0
		
		{
			static uint8 flag = 0u;
			static uint64 FrameNum = 0ull;
			if (!flag)
			{
				FrameNum++;

				flag = 1;
				AddReadbackTexturePass(GraphBuilder, RDG_EVENT_NAME("ReadBackRenderTarget"), RDGRenderTarget,
					[RDGRenderTarget, Size](FRHICommandListImmediate& RHICmdList)
					{
						
						UE_LOG(LogCustomMeshPass, Log, TEXT("ReadBackRenderTarget ,Frame = %d , SizeXY = %s "), FrameNum,*Size.ToString());

						FIntRect Rect(0, 0, Size.X, Size.Y);
					
						TArray<FLinearColor> OutData;
						
						RHICmdList.ReadSurfaceData(RDGRenderTarget->GetRHI(), Rect, OutData, FReadSurfaceDataFlags(RCM_MinMax));

						
						
						for (int32 i = 0; i < Size.Y; ++i)
						{
							bool bFound = false;
							for (int32 j = 0; j < Size.X; ++j)
							{
								FLinearColor x = OutData[i * Size.Y + j];

								if (!x.IsAlmostBlack())
								{
									//UE_LOG(LogCustomMeshPass, Log, TEXT("CSM:  Value Rendered,Frame = %d "), FrameNum);

									UE_LOG(LogCustomMeshPass, Log, TEXT("CSM: X = %d, Y = %d , Frame = %d, Value =  %s")
										, j, i, FrameNum, *x.ToString());
									bFound = true;
									break;
								}

							}
							if (bFound)
								break;
						}
						
					}
				);

			}
			

		}
#endif



		{

			RDG_EVENT_SCOPE(GraphBuilder, "CustomMeshPassCopy");
			RDG_GPU_STAT_SCOPE(GraphBuilder, CustomMeshPassCopy);

			

			if (ResolveTexture.IsValid())
			{
				//RDGRenderTarget->Desc.GetSize()

				UE_LOG(LogCustomMeshPass, Log, TEXT("CopyToResolveTexture"));

				AddCopyTexturePass(GraphBuilder, RDGRenderTarget, RDGResolveTexture, FRHICopyTextureInfo());
			}

		}
	}
}


struct FCustomMeshPassSceneRenderer
{
	

	FSceneInterface* Scene;

	TSet<FPrimitiveSceneProxy*> SceneProxies;

	FTextureRHIRef RenderTarget;

	FTextureRHIRef DepthTarget;

	FTextureRHIRef ResolveTexture;

	FIntPoint RenderTargetSize_Game;

	FIntPoint RenderTargetSize;

	~FCustomMeshPassSceneRenderer()
	{
		check(IsInRenderingThread());

		RenderTarget.SafeRelease();
		DepthTarget.SafeRelease();
		ResolveTexture.SafeRelease();
	}

	void BeginInitResource(FIntPoint InSize)
	{
		InSize = FIntPoint(256,256);

		if (RenderTargetSize_Game != InSize)
		{
			RenderTargetSize_Game = InSize;
			
			ENQUEUE_RENDER_COMMAND(CMPUpdateRenderResource)([=](FRHICommandListImmediate& CML)
			{
				UE_LOG(LogCustomMeshPass, Log, TEXT("CMPUpdateRenderResource.SizeXY = %s"), *RenderTargetSize_Game.ToString());
				RenderTarget.SafeRelease();
				RenderTarget = RHICreateTexture(
				FRHITextureCreateDesc::Create2D(TEXT("CSM Render Target "), RenderTargetSize_Game.X, RenderTargetSize_Game.Y, PF_FloatRGBA)
				.SetFlags(ETextureCreateFlags::RenderTargetable)//| ETextureCreateFlags::UAV)
				.SetClearValue(FClearValueBinding::Black));


				DepthTarget.SafeRelease();
				DepthTarget = RHICreateTexture(
				FRHITextureCreateDesc::Create2D(TEXT("CSM Depth Target "), RenderTargetSize_Game.X, RenderTargetSize_Game.Y, PF_DepthStencil)
				.SetFlags(ETextureCreateFlags::DepthStencilTargetable)
				.SetClearValue(FClearValueBinding::DepthZero));


				RenderTargetSize = RenderTargetSize_Game;

				

				
			}
			);
			
		}
		
	}

	void SetResolveTexture(FTextureRHIRef InResolveTexture)
	{
		ENQUEUE_RENDER_COMMAND(CMPUpdateResolveTexture)([=](FRHICommandListImmediate& CML)
		{
			ResolveTexture = InResolveTexture;
		}
		);
	}

	////must be callded after deferredSceneRenderer::Render UploadDynamicPrimitiveShaderDataForView 2791.h
	void Render_RenderThread(FRDGBuilder& GraphBuilder , const FSceneView& View)
	{
		UE_LOG(LogCustomMeshPass, Log, TEXT("CustomMeshPass Render scene ,scene = %d"),(void*)Scene);

		Render(GraphBuilder ,Scene , View , RenderTargetSize, SceneProxies , RenderTarget , DepthTarget, ResolveTexture);
		
		
	};



};



