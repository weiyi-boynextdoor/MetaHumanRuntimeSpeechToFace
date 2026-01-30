#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimCurveTypes.h"
#include "RuntimeAnimation.generated.h"

UCLASS(BlueprintType, MinimalAPI)
class URuntimeAnimation : public UObject
{
    GENERATED_BODY()

public:
    TArray<FFloatCurve> FloatCurves;;

    float Duration = 0.0f;

    float CurTime = 0.0f;
};
