#include "AnimNode_RuntimeAnim.h"
#include "Animation/AnimCurveUtils.h"

void FAnimNode_RuntimeAnim::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    Super::Update_AnyThread(Context);
    GetEvaluateGraphExposedInputs().Execute(Context);
    DeltaTime = Context.GetDeltaTime();
}

void FAnimNode_RuntimeAnim::Evaluate_AnyThread(FPoseContext& Output)
{
    if (RuntimeAnimation)
    {
        float CurTime = RuntimeAnimation->CurTime;
        if (CurTime >= RuntimeAnimation->Duration)
        {
            return;
        }
        TMap<FName, float> CurveMap;
        CurveMap.Reserve(RuntimeAnimation->FloatCurves.Num());
        for (const FFloatCurve& FloatCurve : RuntimeAnimation->FloatCurves)
        {
            CurveMap.FindOrAdd(FloatCurve.GetName()) = FloatCurve.Evaluate(CurTime);
        }
        FBlendedCurve Curve;
        UE::Anim::FCurveUtils::BuildUnsorted(Curve, CurveMap);
        Output.Curve.Combine(Curve);
        RuntimeAnimation->CurTime += DeltaTime;
    }
}