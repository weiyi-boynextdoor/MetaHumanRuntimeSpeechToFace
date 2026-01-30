#pragma once

#include "Animation/AnimNodeBase.h"
#include "RuntimeAnimation.h"

#include "AnimNode_RuntimeAnim.generated.h"

#define UE_API RUNTIMESPEECHTOFACE_API

USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_RuntimeAnim : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = settings, meta = (AlwaysAsPin))
    TObjectPtr<class URuntimeAnimation> RuntimeAnimation;

    UE_API void Update_AnyThread(const FAnimationUpdateContext& Context) override;

    UE_API void Evaluate_AnyThread(FPoseContext& Output) override;

    float DeltaTime = 0.0f;
};

#undef UE_API
