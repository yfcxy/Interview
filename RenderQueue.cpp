#include "CustomMeshPassRendering/RenderQueue.h"

//#include "CoreMinimal.h"

//#include "Materials/Material.h"
#include "MeshPassProcessor.inl"
#include "RenderGraphUtils.h"
#include "SimpleMeshDrawCommandPass.h"

#include "Shader.h"

#define RENDER_DEPTH 1

#if RENDER_DEPTH



#else



#endif // RENDER_DEPTH

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




class FCSMMeshPassProcessor : public   FMeshPassProcessor
{
public:

	FCSMMeshPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext);

	//begin  FMeshPassProcessor Interface

	 virtual void AddMeshBatch(const FMeshBatch& RESTRICT, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT Proxy, int32 StaticMeshId = -1)override final;

	 FMeshPassProcessorRenderState RenderState;

	//end  FMeshPassProcessor Interface

};

FCSMMeshPassProcessor::FCSMMeshPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand,
FMeshPassDrawListContext* InDrawListContext):FMeshPassProcessor(EMeshPass::Num, Scene, InViewIfDynamicMeshCommand->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
{
	RenderState.SetDepthStencilState(TStaticDepthStencilState<>::GetRHI());
	RenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	RenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilNop);
}

void FCSMMeshPassProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT Proxy, int32 StaticMeshId )
{
	UE_LOG(LogCustomMeshPass, Log, TEXT("AddMeshBatch"));

	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;

	while (MaterialRenderProxy)
	{
		const FMaterial*  MaterialResource = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

		if (MaterialResource)
		{
			//to do :use default material


			const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
			
			TMeshProcessorShaders<FDepthOnlyVS, FDepthOnlyPS> PassShaders;

			FMaterialShaderTypes ShaderTypes;
			ShaderTypes.AddShaderType<FDepthOnlyVS>();
			ShaderTypes.AddShaderType<FCMPPS>();
			FMaterialShaders Shaders;

			if (!MaterialResource->TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
			{
				UE_LOG(LogCustomMeshPass, Log, TEXT("could not find shader "));

				MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);

				continue;
			}
			Shaders.TryGetVertexShader(PassShaders.VertexShader);
			Shaders.TryGetPixelShader(PassShaders.PixelShader);


			const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader,PassShaders.PixelShader);

			

			FCSMShaderElementData ElementData;
			ElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, Proxy, MeshBatch,-1 , true);

			BuildMeshDrawCommands(
				MeshBatch,
				BatchElementMask,
				Proxy,
				*MaterialRenderProxy,
				*MaterialResource,
				RenderState,
				PassShaders,
				ERasterizerFillMode::FM_Solid,
				ERasterizerCullMode::CM_CW,
				SortKey,
				EMeshPassFeatures::Default,
				ElementData
			);

			UE_LOG(LogCustomMeshPass, Log, TEXT("build mesh draw command "));

			break;
			
		}
		else
		{
			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}

		
	}

	
}



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


#include "SceneViewExtension.h"

/*
	Scene View Extension
*/

class FCSMSceneViewExtension : public FWorldSceneViewExtension
{
public:

	FCSMSceneViewExtension(const FAutoRegister& Register, UWorld* World) :FWorldSceneViewExtension(Register, World)
	{
		bInitialized = false;
		WorldCached = World;

		FSceneInterface* Scene = World->Scene;

		SceneRender = new FCustomMeshPassSceneRenderer;
		SceneRender->Scene = Scene;

		ENQUEUE_RENDER_COMMAND(AllocateCMSRender)([Scene  , Renderer = SceneRender](FRHICommandListImmediate& CML)
		{
			check(FindByScene(Scene) == NULL);

			SceneRendererMap.Add(Scene, Renderer);

			//set resolve target

		});
	}

	virtual ~FCSMSceneViewExtension()
	{
		SceneRender = NULL;

		ENQUEUE_RENDER_COMMAND(DeleteCMSRender)([Scene = WorldCached->Scene](FRHICommandListImmediate& CML)
		{
			FCustomMeshPassSceneRenderer* Renderer = SceneRendererMap.FindAndRemoveChecked(Scene);
			delete Renderer;

		}
		);
	}

	UWorld* WorldCached;

	FCustomMeshPassSceneRenderer* SceneRender;

	uint32 bInitialized : 1;

	//begin ISceneViewExtension Interface

	
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily)override
	{
		//UE_LOG(LogCustomMeshPass, Log, TEXT("View Extension : SetupViewFamily "));


		if (!bInitialized)
		{
			bInitialized = true;


		}
		
		FIntPoint SizeXY = InViewFamily.RenderTarget->GetSizeXY();

		SceneRender->BeginInitResource(SizeXY);
		

	}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)override
	{

	}

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily)override
	{
		
	}

	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)override
	{
		FCustomMeshPassSceneRenderer* Renderer = FindByScene(InViewFamily.Scene);

		check(Renderer);

		FIntPoint SizeXY = InViewFamily.RenderTarget->GetSizeXY();

		for (auto& View : InViewFamily.Views)
		{
			UE_LOG(LogCustomMeshPass, Log, TEXT("View Extension : PostRenderViewFamily "));

			Renderer->Render_RenderThread(GraphBuilder, *View);
		}
		
	}

	virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)override
	{
		
	}

	//end ISceneViewExtension Interface



	static TMap<FSceneInterface*, FCustomMeshPassSceneRenderer*> SceneRendererMap ;

	static FCustomMeshPassSceneRenderer* FindByScene(FSceneInterface* Scene)
	{
		auto * item = SceneRendererMap.Find(Scene);
		if (item)
		{
			return *item;
		}
		return NULL;
	}
};

TMap<FSceneInterface*, FCustomMeshPassSceneRenderer*> FCSMSceneViewExtension::SceneRendererMap;


namespace CustomMeshPassPrivate
{
	FDelegateHandle PostWorldInitialization;
	FDelegateHandle OnWorldCleanup;

	typedef TSharedPtr<FCSMSceneViewExtension, ESPMode::ThreadSafe> SceneViewExtensionRef;

	struct FCachedState
	{
		UWorld* World;

		SceneViewExtensionRef ViewExtension;
	
	};

	TArray<FCachedState> States;


	struct FCustomMeshPassRegister
	{

		FCustomMeshPassRegister()
		{
			//UE_LOG(LogCustomMeshPass, Log, TEXT("CustomMeshPass Register "));

			CustomMeshPassPrivate::PostWorldInitialization = FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues IVS)
			{
				//UE_LOG(LogCustomMeshPass, Log, TEXT(" OnPostWorldInitialization "));
				auto Scene = World->Scene;
				if (Scene)
				{
					//UE_LOG(LogCustomMeshPass, Log, TEXT(" create scene renderer ,world = %s ,scene = %d"), *World->GetMapName(),(void*)Scene);

					auto& State = States.AddDefaulted_GetRef();
					
					State.World = World;
					State.ViewExtension = FSceneViewExtensions::NewExtension<FCSMSceneViewExtension>(World);
				
					
				}
				else
				{
					//UE_LOG(LogCustomMeshPass, Log, TEXT("world has no scene "));

				}
			}
			);

			CustomMeshPassPrivate::OnWorldCleanup = FWorldDelegates::OnPostWorldCleanup.AddLambda([](UWorld* World, bool bSessionEnded, bool bCleanupResources)
			{

				//UE_LOG(LogCustomMeshPass, Log, TEXT("world Cleanup "));
				auto Scene = World->Scene;
				if (Scene)
				{
					//UE_LOG(LogCustomMeshPass, Log, TEXT("delete scene renderer ,world = %s, ,scene = %d"), *World->GetMapName(), (void*)Scene);

					int32 FoundId = INDEX_NONE;
					for (int32 i = 0 ; i < States.Num();i++)
					{
						if (States[i].World == World)
						{
							FoundId = i;

							break;
						}


					}
					check(FoundId != INDEX_NONE);
					
					States.RemoveAtSwap(FoundId);

				}
				else
				{
					//UE_LOG(LogCustomMeshPass, Log, TEXT("world has no scene "));

				}
			}
			);
		}
	};

	FCustomMeshPassRegister Register;
}

#include "Components/PrimitiveComponent.h"
#include "Engine/TextureRenderTarget2D.h"

void FCustomMeshPassStatic::AddPrimitive_Concurrent(UWorld* World, UPrimitiveComponent* Comp)
{
	ENQUEUE_RENDER_COMMAND(CustomMeshPassAddPrimitiveToScene)([Scene = World->Scene, Proxy = Comp->SceneProxy,MapName = World->GetMapName()](FRHICommandListImmediate& CML)
	{

		FCustomMeshPassSceneRenderer* Renderer = FCSMSceneViewExtension::FindByScene(Scene);
		
		check(Renderer);

		if (Renderer)
		{
			check(Renderer->SceneProxies.Find(Proxy) == NULL);
			Renderer->SceneProxies.Add(Proxy);

			//UE_LOG(LogCustomMeshPass, Log, TEXT(" AddPrimitive ,World = %s,Scene = %d"),*MapName,(void*)Scene);
		}
	});
}

void FCustomMeshPassStatic::RemovePrimitive_Concurrent(UWorld* World, UPrimitiveComponent* Comp)
{
	ENQUEUE_RENDER_COMMAND(CustomMeshPassRemovePrimitiveFromScene)([Scene = World->Scene, Proxy = Comp->SceneProxy, MapName = World->GetMapName()](FRHICommandListImmediate& CML)
	{
		FCustomMeshPassSceneRenderer* Renderer = FCSMSceneViewExtension::FindByScene(Scene);

		check(Renderer);
		

		if (Renderer)
		{
			check(Renderer->SceneProxies.Find(Proxy) != NULL);
			Renderer->SceneProxies.Remove(Proxy);

			//UE_LOG(LogCustomMeshPass, Log, TEXT(" RemovePrimitive,World = %s ,Scene = %d"), *MapName, (void*)Scene);
		}

	}
	);
}

void FCustomMeshPassStatic::SetResolveTexture(UWorld* World, UTextureRenderTarget2D* InTexture)
{
	if (!IsValid(World))
	{
		return;
	}
	FTextureReferenceRHIRef Resolve = IsValid(InTexture) ? InTexture->TextureReference.TextureReferenceRHI : NULL;

	ENQUEUE_RENDER_COMMAND(CustomMeshPassRemovePrimitiveFromScene)([Scene = World->Scene,
		Resolve = IsValid(InTexture) ? InTexture->TextureReference.TextureReferenceRHI : NULL](FRHICommandListImmediate& CML)
	{
		FCustomMeshPassSceneRenderer* Renderer = FCSMSceneViewExtension::FindByScene(Scene);

		check(Renderer);


		if (Renderer)
		{
			
			Renderer->ResolveTexture = Resolve->GetReferencedTexture();
		}

	}
	);
}

