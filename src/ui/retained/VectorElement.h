// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Vector.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Decorator.h>

namespace openq4::ui {
class Host;
struct Node;
struct RuntimeStatistics;
class VectorGeometry {
public:
	void Configure(const std::vector<VectorPath>& paths, Host& host, RuntimeStatistics& statistics);
	// Copy authored paths and shared host ownership only; compiled geometry is
	// always invalidated, including when replacing an already rendered copy.
	void CopyArtworkFrom(const VectorGeometry& source);
	void Render(Rml::Element& element, bool inheritOpacity);
	bool HitTest(Rml::Element& element, Rml::Vector2f outputPoint);
	bool HasHitArea(Rml::Element& element);
private:
	bool PrepareHitGeometry(Rml::Element& element, Rml::Vector2f& origin);
	std::vector<VectorMesh> hitGeometry;
	std::array<double,6> hitSignature{};
	bool hitPrepared = false, hitValid = false, hitArea = false;
	std::vector<VectorPath> paths;
	std::vector<VectorMesh> compiled;
	std::vector<Rml::Geometry> geometry;
	Host* host = nullptr;
	RuntimeStatistics* statistics = nullptr;
	std::array<double,12> previous{};
	double previousOpacity = -1;
	bool valid = false;
};
class VectorElement final : public Rml::Element {
public:
	explicit VectorElement(const Rml::String& tag) : Rml::Element(tag) {}
	void Configure(const Node& node, Host& host, RuntimeStatistics& statistics);
	void CopyArtworkFrom(const VectorElement& source);
	void RenderMask() { mask.Render(*this,false); }
	bool AllowsMaskedPoint(Rml::Vector2f point) { return !hasMask || mask.HitTest(*this,point); }
	bool HasMaskArea() { return !hasMask || mask.HasHitArea(*this); }
protected:
	void OnRender() override { paint.Render(*this,true); }
private:
	VectorGeometry paint, mask;
	bool hasMask = false;
};
class VectorMaskInstancer final : public Rml::DecoratorInstancer {
public:
	VectorMaskInstancer();
	Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary&,
		const Rml::DecoratorInstancerInterface&) override;
};
} // namespace openq4::ui
