#pragma once

namespace Raven::ph
{

// 天体として重力相互作用へ参加することを示すECS Componentです。
// 質量はRigidBodyComponent::Massを正本とし、ここへ重複保持しません。
struct CelestialBodyComponent
{
    bool GenerateGravity = true;
    bool ReceiveGravity = true;
};

} // namespace Raven::ph
