#pragma once

#include "Raven/Animation/AnimationKeyframe.h"
#include "Raven/Math/MathQuatanion.h"
#include "Raven/Math/MathVector.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace Raven
{

// ============================================================================
// AnimationInterpolation
// ============================================================================
// Curve区間の補間方式です。
//
// Step / LinearをAnimation Coreの共通表現として持つことで、3D Transformだけでなく、
// UI Property、2D Character Parameter、Morph Weightなども同じCurve評価器へ載せられます。
// Cubic系は接線データをKeyframeへ追加する必要があるため、誤った近似実装をせず後続段階で追加します。
enum class AnimationInterpolation
{
    Step,
    Linear
};

// ============================================================================
// AnimationValueInterpolator
// ============================================================================
// AnimationCurve<T>から値型固有の補間方法を分離します。
// float / Vec2 / Vec3 / Vec4のように加算・scalar乗算を持つ型は既定実装を利用できます。
// Quaternionは線形補間では回転速度や姿勢が不自然になるため専用特殊化でSlerpします。
template <typename T>
struct AnimationValueInterpolator
{
    static T Prepare(const T& value)
    {
        return value;
    }

    static T Linear(const T& from, const T& to, float alpha)
    {
        return from * (1.0f - alpha) + to * alpha;
    }
};

template <>
struct AnimationValueInterpolator<math::Quat>
{
    static math::Quat Prepare(const math::Quat& value)
    {
        return value.Normalized();
    }

    static math::Quat Linear(const math::Quat& from, const math::Quat& to, float alpha)
    {
        return math::Quat::Slerp(from, to, alpha);
    }
};

// ============================================================================
// AnimationCurve
// ============================================================================
// 時刻付きKey列と補間規則を所有する汎用Animation Curveです。
//
// AnimationCurveは「何のPropertyを動かすか」を知りません。
// Target/Property Bindingは別レイヤーへ分離し、Curveは時刻から値を評価する責務だけを持ちます。
// この分離により同じCurveを3D Bone、UI、2D Deformer Parameterなどへ再利用できます。
template <typename T>
class AnimationCurve
{
public:
    using Keyframe = AnimationKeyframe<T>;
    using KeyContainer = std::vector<Keyframe>;

    AnimationCurve() = default;

    explicit AnimationCurve(AnimationInterpolation interpolation)
        : m_Interpolation(interpolation)
    {
    }

    AnimationCurve& operator=(std::initializer_list<Keyframe> keys)
    {
        m_Keys = keys;
        return *this;
    }

    // 既存Importerはstd::vector<AnimationKeyframe<T>>&を出力先として受け取ります。
    // Generic Curve移行の第1段階では呼び出し側を一度に壊さないため、KeyContainerへの互換変換を残します。
    // Property Binding導入後にImporter APIもAnimationCurve<T>&へ寄せ、この互換経路を整理します。
    operator KeyContainer&()
    {
        return m_Keys;
    }

    operator const KeyContainer&() const
    {
        return m_Keys;
    }

    AnimationInterpolation GetInterpolation() const
    {
        return m_Interpolation;
    }

    void SetInterpolation(AnimationInterpolation interpolation)
    {
        m_Interpolation = interpolation;
    }

    KeyContainer& GetKeys()
    {
        return m_Keys;
    }

    const KeyContainer& GetKeys() const
    {
        return m_Keys;
    }

    bool Empty() const
    {
        return m_Keys.empty();
    }

    std::size_t GetKeyCount() const
    {
        return m_Keys.size();
    }

    void Clear()
    {
        m_Keys.clear();
    }

    // 指定時刻の値を評価します。
    // Keyが無い場合はdefaultValue、範囲外では端のKeyへClampします。
    // RuntimeのLoopや再生方向はAnimationPlayer/Animator側の責務とし、Curveへ持ち込みません。
    T Sample(float time, const T& defaultValue) const
    {
        if (m_Keys.empty())
        {
            return AnimationValueInterpolator<T>::Prepare(defaultValue);
        }

        if (m_Keys.size() == 1u || time <= m_Keys.front().Time)
        {
            return AnimationValueInterpolator<T>::Prepare(m_Keys.front().Value);
        }

        if (time >= m_Keys.back().Time)
        {
            return AnimationValueInterpolator<T>::Prepare(m_Keys.back().Value);
        }

        const auto rightIt = std::upper_bound(
            m_Keys.begin(),
            m_Keys.end(),
            time,
            [](float sampleTime, const Keyframe& key)
            {
                return sampleTime < key.Time;
            });

        const auto leftIt = rightIt - 1;
        const float interval = rightIt->Time - leftIt->Time;

        // Importerで同時刻Keyを拒否するのが基本ですが、Runtime側でも0除算を防ぎます。
        // 不正Curveを評価した場合は右Keyを採用し、NaNをAnimation全体へ伝播させません。
        if (interval <= 0.0f)
        {
            return AnimationValueInterpolator<T>::Prepare(rightIt->Value);
        }

        if (m_Interpolation == AnimationInterpolation::Step)
        {
            return AnimationValueInterpolator<T>::Prepare(leftIt->Value);
        }

        const float alpha = (time - leftIt->Time) / interval;
        return AnimationValueInterpolator<T>::Prepare(
            AnimationValueInterpolator<T>::Linear(leftIt->Value, rightIt->Value, alpha));
    }

private:
    KeyContainer m_Keys;
    AnimationInterpolation m_Interpolation = AnimationInterpolation::Linear;
};

// ============================================================================
// AnimationPropertyBinding
// ============================================================================
// 外部Animation DataとRaven Runtime Propertyを結ぶ論理Bindingです。
//
// TargetPathは階層PathやParameter Owner IDなど、Importerが保持できる安定した識別子を想定します。
// 空文字は「現在の再生対象自身」を意味するため有効です。
// PropertyはPosition / Opacity / ParamAngleXなど、Target内のProperty識別子です。
//
// Runtimeで毎Frame文字列検索するとUI/2D CharacterのTrack数が増えた際に負荷になるため、
// 後続のBinding Resolverで一度だけ実Target/Property Handleへ解決する前提にします。
struct AnimationPropertyBinding
{
    std::string TargetPath;
    std::string Property;

    bool IsValid() const
    {
        return Property.empty() == false;
    }

    bool operator==(const AnimationPropertyBinding& other) const
    {
        return TargetPath == other.TargetPath && Property == other.Property;
    }

    bool operator!=(const AnimationPropertyBinding& other) const
    {
        return (*this == other) == false;
    }
};

// ============================================================================
// PropertyAnimationTrack<T>
// ============================================================================
// 1つのProperty Bindingと1本の型付きCurveを対応付けます。
// Animation CoreはUIElementやLive2D Parameter等の具体型を知らず、Bindingと値だけを保持します。
template <typename T>
struct PropertyAnimationTrack
{
    using ValueType = T;

    AnimationPropertyBinding Binding;
    AnimationCurve<T> Curve;

    bool Empty() const
    {
        return Curve.Empty();
    }
};

// 現段階でRuntime Propertyとして共通化する基本値型です。
// ColorはVec4、2D位置/SizeはVec2、3D/Morph Parameterはfloat/Vec3などへ対応できます。
// 整数Frame Indexやbool Eventは補間意味が異なるため、Sprite/Event実装時に別Track種別として追加します。
using AnimationPropertyValue = std::variant<
    float,
    math::Vec2,
    math::Vec3,
    math::Vec4,
    math::Quat>;

using AnimationPropertyTrack = std::variant<
    PropertyAnimationTrack<float>,
    PropertyAnimationTrack<math::Vec2>,
    PropertyAnimationTrack<math::Vec3>,
    PropertyAnimationTrack<math::Vec4>,
    PropertyAnimationTrack<math::Quat>>;

struct AnimationPropertySample
{
    AnimationPropertyBinding Binding;
    AnimationPropertyValue Value;
};

inline const AnimationPropertyBinding& GetAnimationPropertyBinding(const AnimationPropertyTrack& track)
{
    return std::visit(
        [](const auto& typedTrack) -> const AnimationPropertyBinding&
        {
            return typedTrack.Binding;
        },
        track);
}

inline bool IsAnimationPropertyTrackEmpty(const AnimationPropertyTrack& track)
{
    return std::visit(
        [](const auto& typedTrack)
        {
            return typedTrack.Empty();
        },
        track);
}

inline AnimationPropertySample SampleAnimationPropertyTrack(
    const AnimationPropertyTrack& track,
    float time)
{
    return std::visit(
        [time](const auto& typedTrack) -> AnimationPropertySample
        {
            using ValueType = typename std::decay_t<decltype(typedTrack)>::ValueType;

            AnimationPropertySample sample;
            sample.Binding = typedTrack.Binding;
            sample.Value = typedTrack.Curve.Sample(time, ValueType{});
            return sample;
        },
        track);
}

// ============================================================================
// TransformAnimationTrack
// ============================================================================
// 1つのTransformに対するPosition / Rotation / ScaleのCurveです。
//
// Curve自体はTargetやBoneを知らず、時刻から値を評価する責務だけを持ちます。
// TransformAnimationTrackは3D Transform向けのChannel構成だけを定義し、
// UI / 2D Character Parameterなどは同じAnimationCurve<T>を別Track構成で再利用できます。
//
// PositionKeys / RotationKeys / ScaleKeysという既存名は、Scene側とglTF Importerの
// 移行影響を小さくするため第1段階では維持します。型はvectorからAnimationCurveへ変わっており、
// 補間規則とSample処理はCurve側へ集約されています。
struct TransformAnimationTrack
{
    AnimationCurve<math::Vec3> PositionKeys;
    AnimationCurve<math::Quat> RotationKeys;
    AnimationCurve<math::Vec3> ScaleKeys;

    bool Empty() const
    {
        return PositionKeys.Empty() && RotationKeys.Empty() && ScaleKeys.Empty();
    }
};

} // namespace Raven
