#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_RuntimeAnim.h"
#include "AnimGraphNode_RuntimeAnim.generated.h"

UCLASS(MinimalAPI, meta = ( Keywords = "Runtime Animation Node" ))
class UAnimGraphNode_RuntimeAnim : public UAnimGraphNode_Base
{
    GENERATED_UCLASS_BODY()

public:
    UPROPERTY(EditAnywhere, Category=Settings)
    FAnimNode_RuntimeAnim Node;
};