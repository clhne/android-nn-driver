//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "ConversionUtils.hpp"

///
/// Helper classes
///

namespace armnn_driver
{

LayerInputHandle::LayerInputHandle()
    : m_OutputSlot(nullptr)
    , m_Valid(false)
{}

LayerInputHandle::LayerInputHandle(bool valid, armnn::IOutputSlot* outputSlot, armnn::TensorInfo tensorInfo)
    : m_OutputSlot(outputSlot)
    , m_Valid(valid)
    , m_TensorInfo(tensorInfo)
{}

bool LayerInputHandle::IsValid() const
{
    return m_Valid;
}

void LayerInputHandle::Connect(armnn::IInputSlot& inputSlot)
{
    BOOST_ASSERT(IsValid());
    if (m_OutputSlot)
    {
        m_OutputSlot->Connect(inputSlot);
    }
}

const armnn::TensorInfo& LayerInputHandle::GetTensorInfo() const
{
    return m_TensorInfo;
}

ConstTensorPin::ConstTensorPin(bool optional)
    : m_Optional(optional)
{}

ConstTensorPin::ConstTensorPin(const armnn::TensorInfo& tensorInfo,
                               const void* valueStart,
                               uint32_t numBytes,
                               const armnn::PermutationVector& mappings)
{
    boost::ignore_unused(numBytes);
    assert(tensorInfo.GetNumBytes() == numBytes);

    const bool needsSwizzling = (mappings.GetSize() > 0);
    if (needsSwizzling)
    {
        m_SwizzledTensorData.resize(tensorInfo.GetNumBytes());
        SwizzleAndroidNn4dTensorToArmNn(tensorInfo, valueStart, m_SwizzledTensorData.data(), mappings);

        m_ConstTensor = armnn::ConstTensor(armnnUtils::Permuted(tensorInfo, mappings), m_SwizzledTensorData.data());
    }
    else
    {
        m_ConstTensor = armnn::ConstTensor(tensorInfo, valueStart);
    }
}

bool ConstTensorPin::IsValid() const
{
    return m_ConstTensor.GetMemoryArea() != nullptr;
}

bool ConstTensorPin::IsOptional() const
{
    return m_Optional;
}

const armnn::ConstTensor& ConstTensorPin::GetConstTensor() const
{
    return m_ConstTensor;
}

const armnn::ConstTensor* ConstTensorPin::GetConstTensorPtr() const
{
    if (IsValid() && m_ConstTensor.GetNumElements() > 0)
    {
        return &m_ConstTensor;
    }
    // tensor is either invalid, or has no elements (indicating an optional tensor that was not provided)
    return nullptr;
}

///
/// Utility functions
///

armnn::IConnectableLayer* ProcessActivation(const armnn::TensorInfo& tensorInfo,
                                            ActivationFn activation,
                                            armnn::IConnectableLayer* prevLayer,
                                            ConversionData& data)
{
    BOOST_ASSERT(prevLayer->GetNumOutputSlots() == 1);

    prevLayer->GetOutputSlot(0).SetTensorInfo(tensorInfo);

    armnn::IConnectableLayer* activationLayer = prevLayer;

    if (activation != ActivationFn::kActivationNone)
    {
        armnn::ActivationDescriptor activationDesc;
        switch (activation)
        {
            case ActivationFn::kActivationRelu:
            {
                activationDesc.m_Function = armnn::ActivationFunction::ReLu;
                break;
            }
            case ActivationFn::kActivationRelu1:
            {
                activationDesc.m_Function = armnn::ActivationFunction::BoundedReLu;
                activationDesc.m_A = 1.0f;
                activationDesc.m_B = -1.0f;
                break;
            }
            case ActivationFn::kActivationRelu6:
            {
                activationDesc.m_Function = armnn::ActivationFunction::BoundedReLu;
                activationDesc.m_A = 6.0f;
                break;
            }
            case ActivationFn::kActivationSigmoid:
            {
                activationDesc.m_Function = armnn::ActivationFunction::Sigmoid;
                break;
            }
            case ActivationFn::kActivationTanh:
            {
                activationDesc.m_Function = armnn::ActivationFunction::TanH;
                activationDesc.m_A = 1.0f;
                activationDesc.m_B = 1.0f;
                break;
            }
            default:
            {
                Fail("%s: Invalid activation enum value %i", __func__, activation);
                return nullptr;
            }
        }

        if (!IsLayerSupported(__func__,
                              armnn::IsActivationSupported,
                              data.m_Compute,
                              prevLayer->GetOutputSlot(0).GetTensorInfo(),
                              tensorInfo,
                              activationDesc))
        {
            return nullptr;
        }

        activationLayer = data.m_Network->AddActivationLayer(activationDesc);

        prevLayer->GetOutputSlot(0).Connect(activationLayer->GetInputSlot(0));
        activationLayer->GetOutputSlot(0).SetTensorInfo(tensorInfo);
    }

    return activationLayer;
}

} // namespace armnn_driver