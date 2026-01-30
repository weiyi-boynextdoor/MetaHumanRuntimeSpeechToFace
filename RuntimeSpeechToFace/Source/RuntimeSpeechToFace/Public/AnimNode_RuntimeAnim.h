#pragma once

#include "Animation/AnimNodeBase.h"
#include "RuntimeAnimation.h"

#include "AnimNode_RuntimeAnim.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_RuntimeAnim : public FAnimNode_Base
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = settings, meta = (AlwaysAsPin))
    TObjectPtr<class URuntimeAnimation> RuntimeAnimation;
};
