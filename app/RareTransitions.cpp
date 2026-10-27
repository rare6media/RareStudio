#include "RareTransitions.h"

#include <obs.h>

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace RareTransitions {
namespace {

enum Mode {
	Smooth = 0,
	ZoomBlur,
	Whip,
	Glitch,
	Iris,
	Spin,
	Pixelate,
	Ripple,
	Flash,
	BlurDissolve,
	Clock,
	BrandStinger,
};

struct Desc {
	const char *id;
	const char *name;
	Mode mode;
	float intensity; // default strength
	bool direction;
	bool softness;
	bool color;
	bool logo;
};

const Desc kDescs[] = {
	{"rare_smooth", "Smooth Dissolve (eased)", Smooth, 1.0f, false, false, false, false},
	{"rare_zoom_blur", "Zoom Blur", ZoomBlur, 1.0f, false, false, false, false},
	{"rare_whip", "Whip Pan (motion blur)", Whip, 1.0f, true, false, false, false},
	{"rare_glitch", "Glitch", Glitch, 1.0f, false, false, false, false},
	{"rare_iris", "Iris (circle reveal)", Iris, 1.0f, false, true, false, false},
	{"rare_spin", "Spin Zoom", Spin, 1.0f, false, false, false, false},
	{"rare_pixelate", "Pixelate", Pixelate, 1.0f, false, false, false, false},
	{"rare_ripple", "Shockwave Ripple", Ripple, 1.0f, false, false, false, false},
	{"rare_flash", "Flash Through Color", Flash, 1.0f, false, false, true, false},
	{"rare_blur_dissolve", "Blur Dissolve", BlurDissolve, 1.0f, false, false, false, false},
	{"rare_clock", "Clock Wipe", Clock, 1.0f, false, true, false, false},
	{"rare_stinger_brand", "Brand Stinger (no video needed)", BrandStinger, 1.0f, false, false, true, true},
};
constexpr int kCount = int(sizeof(kDescs) / sizeof(kDescs[0]));

// One pixel shader for every transition; `mode` picks the look. Uniform branching is cheap on
// a GPU and keeps this to a single effect to compile. p = eased progress (may overshoot for the
// "back" curve), m = p clamped for colour mixing, t = raw linear progress.
const char *kEffect = R"FX(
uniform float4x4 ViewProj;
uniform texture2d tex_a;
uniform texture2d tex_b;
uniform texture2d tex_logo;
uniform float progress;
uniform float raw;
uniform int   mode;
uniform float intensity;
uniform float softness;
uniform float2 dir;
uniform float2 res;
uniform float4 color_a;
uniform float4 color_b;
uniform int   has_a;
uniform int   has_b;
uniform int   has_logo;

sampler_state samp {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertData {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertData VSDefault(VertData v_in)
{
	VertData v;
	v.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
	v.uv  = v_in.uv;
	return v;
}

float4 SA(float2 uv)
{
	if (has_a == 0)
		return float4(0.0, 0.0, 0.0, 1.0);
	return tex_a.Sample(samp, uv);
}

float4 SB(float2 uv)
{
	if (has_b == 0)
		return float4(0.0, 0.0, 0.0, 1.0);
	return tex_b.Sample(samp, uv);
}

float hash11(float x)
{
	return frac(sin(x * 91.3458) * 47453.5453);
}

float2 spin(float2 uv, float ang, float zoom, float aspect)
{
	float2 d = uv - float2(0.5, 0.5);
	d.x *= aspect;
	float c = cos(ang);
	float s = sin(ang);
	d = float2(d.x * c - d.y * s, d.x * s + d.y * c) / zoom;
	d.x /= aspect;
	return d + float2(0.5, 0.5);
}

float4 PSTransition(VertData v_in) : TARGET
{
	float2 uv = v_in.uv;
	float aspect = res.x / res.y;
	float p = progress;
	float m = saturate(progress);
	float4 outc = float4(0.0, 0.0, 0.0, 1.0);

	if (mode == 0) {
		outc = lerp(SA(uv), SB(uv), m);

	} else if (mode == 1) {
		// Zoom blur: A rushes toward the camera, B settles in from a zoom.
		float2 c = float2(0.5, 0.5);
		float2 d = uv - c;
		float amt = 0.35 * intensity;
		float4 acc_a = float4(0.0, 0.0, 0.0, 0.0);
		float4 acc_b = float4(0.0, 0.0, 0.0, 0.0);
		for (int i = 0; i < 10; i++) {
			float k = float(i) / 10.0;
			acc_a += SA(c + d * (1.0 - amt * m * k));
			acc_b += SB(c + d * (1.0 - amt * (1.0 - m) * k));
		}
		outc = lerp(acc_a / 10.0, acc_b / 10.0, smoothstep(0.30, 0.70, m));

	} else if (mode == 2) {
		// Whip pan: both pictures slide with directional motion blur.
		float2 ua = uv + dir * p;
		float2 ub = uv + dir * (p - 1.0);
		float blur = sin(3.14159 * m) * 0.06 * intensity;
		float4 acc = float4(0.0, 0.0, 0.0, 0.0);
		for (int i = 0; i < 12; i++) {
			float2 o = dir * blur * (float(i) / 11.0 - 0.5);
			float2 qa = ua + o;
			float2 qb = ub + o;
			if (qa.x >= 0.0 && qa.x <= 1.0 && qa.y >= 0.0 && qa.y <= 1.0)
				acc += SA(qa);
			else
				acc += SB(qb);
		}
		outc = acc / 12.0;

	} else if (mode == 3) {
		// Glitch: row-displaced blocks, RGB split, per-row reveal and a little scanline flicker.
		float g = sin(3.14159 * m) * intensity;
		float frame = floor(raw * 24.0);
		float row = floor(uv.y * 48.0);
		float h = hash11(row + frame * 7.0);
		float shift = (h - 0.5) * 0.22 * g * step(0.55, h);
		float2 q = float2(uv.x + shift, uv.y);
		float split = 0.018 * g;
		float pick = step(hash11(floor(uv.y * 24.0) + frame * 3.0 + 11.0), m);
		float4 ca;
		float4 cb;
		ca.r = SA(q + float2(split, 0.0)).r;
		ca.g = SA(q).g;
		ca.b = SA(q - float2(split, 0.0)).b;
		ca.a = 1.0;
		cb.r = SB(q + float2(split, 0.0)).r;
		cb.g = SB(q).g;
		cb.b = SB(q - float2(split, 0.0)).b;
		cb.a = 1.0;
		outc = lerp(ca, cb, pick);
		float scan = 1.0 - 0.12 * g * (0.5 + 0.5 * sin(uv.y * res.y * 1.5));
		outc.rgb *= scan;

	} else if (mode == 4) {
		// Iris: a soft-edged circle opens from the centre and reveals B.
		float2 d = (uv - float2(0.5, 0.5)) * float2(aspect, 1.0);
		float r = length(d) / (0.5 * sqrt(aspect * aspect + 1.0));
		float soft = max(softness, 0.02);
		float edge = p * (1.0 + soft);
		float k = 1.0 - smoothstep(edge - soft, edge, r);
		outc = lerp(SA(uv), SB(uv), k);

	} else if (mode == 5) {
		// Spin zoom.
		float ang = 1.2 * intensity;
		float4 a = SA(spin(uv, ang * m, 1.0 + 0.6 * m, aspect));
		float4 b = SB(spin(uv, -ang * (1.0 - m), 1.0 + 0.6 * (1.0 - m), aspect));
		outc = lerp(a, b, smoothstep(0.25, 0.75, m));

	} else if (mode == 6) {
		// Pixelate up, swap, pixelate back down.
		float size = max(1.0, floor(sin(3.14159 * m) * 64.0 * intensity));
		float2 q = (floor(uv * res / size) + float2(0.5, 0.5)) * size / res;
		outc = lerp(SA(q), SB(q), smoothstep(0.42, 0.58, m));

	} else if (mode == 7) {
		// Shockwave: a ring races outward, bending the picture and revealing B behind it.
		float2 d = (uv - float2(0.5, 0.5)) * float2(aspect, 1.0);
		float r = length(d);
		float R = p * (0.5 * sqrt(aspect * aspect + 1.0) + 0.15);
		float w = 0.07 * intensity;
		float ring = exp(-pow((r - R) / w, 2.0));
		float2 dn = d / max(r, 0.0001);
		float2 disp = dn * ring * 0.05 * intensity * float2(1.0 / aspect, 1.0);
		float k = smoothstep(R + 0.01, R - 0.01, r);
		outc = lerp(SA(uv - disp), SB(uv - disp), k);
		outc.rgb += ring * 0.12 * intensity;

	} else if (mode == 8) {
		// Flash through a colour (white by default) with a short hold at the peak.
		float f = 1.0 - abs(2.0 * m - 1.0);
		f = pow(saturate(f * 1.35), 2.0);
		float4 base = (m < 0.5) ? SA(uv) : SB(uv);
		outc = lerp(base, float4(color_a.rgb, 1.0), saturate(f * intensity));

	} else if (mode == 9) {
		// Blur dissolve: defocus A, cross over, refocus on B.
		float rad = sin(3.14159 * m) * 0.02 * intensity;
		float4 acc_a = float4(0.0, 0.0, 0.0, 0.0);
		float4 acc_b = float4(0.0, 0.0, 0.0, 0.0);
		for (int i = 0; i < 16; i++) {
			float a = float(i) * 2.39996;
			float rr = sqrt((float(i) + 0.5) / 16.0) * rad;
			float2 o = float2(cos(a) * rr / aspect, sin(a) * rr);
			acc_a += SA(uv + o);
			acc_b += SB(uv + o);
		}
		outc = lerp(acc_a / 16.0, acc_b / 16.0, smoothstep(0.30, 0.70, m));

	} else if (mode == 10) {
		// Clock wipe, starting at 12 o'clock.
		float2 d = uv - float2(0.5, 0.5);
		float ang = atan2(d.x, -d.y) / 6.2831853 + 0.5;
		float soft = max(softness, 0.01);
		float edge = m * (1.0 + soft);
		float k = 1.0 - smoothstep(edge - soft, edge, ang);
		outc = lerp(SA(uv), SB(uv), k);

	} else {
		// Brand stinger: diagonal colour bars sweep across and cover the scene change, a logo
		// pops at the centre, then the bars exit revealing B.
		float4 base = (m < 0.5) ? SA(uv) : SB(uv);
		float ang = -0.35;
		float2 d = uv - float2(0.5, 0.5);
		d.x *= aspect;
		float x = (d.x * cos(ang) - d.y * sin(ang)) / (aspect * 1.2) + 0.5;
		float y = (d.x * sin(ang) + d.y * cos(ang)) + 0.5;
		float bars = 6.0;
		float idx = clamp(floor(y * bars), 0.0, bars - 1.0);
		float lag = idx * 0.045;
		float inT = smoothstep(lag, lag + 0.30, m * 1.0);
		float outT = smoothstep(0.55 + lag, 0.55 + lag + 0.30, m);
		float right = lerp(-0.05, 1.10, inT);
		float left = lerp(-0.05, 1.10, outT);
		float4 col = lerp(color_a, color_b, frac(idx * 0.5));
		float coverage = step(left, x) * step(x, right);
		outc = lerp(base, col, coverage);

		if (has_logo != 0) {
			float pop = smoothstep(0.30, 0.45, m) * (1.0 - smoothstep(0.58, 0.72, m));
			float s = 0.30 * (0.6 + 0.4 * pop);
			float2 l = (uv - float2(0.5, 0.5)) * float2(aspect, 1.0) / s + float2(0.5, 0.5);
			if (l.x >= 0.0 && l.x <= 1.0 && l.y >= 0.0 && l.y <= 1.0) {
				float4 lc = tex_logo.Sample(samp, l);
				outc.rgb = lerp(outc.rgb, lc.rgb, lc.a * pop);
			}
		}
	}
	outc.a = 1.0;
	return outc;
}

technique Transition
{
	pass
	{
		vertex_shader = VSDefault(v_in);
		pixel_shader  = PSTransition(v_in);
	}
}
)FX";

enum Easing { Linear, SmoothCurve, Snappy, Overshoot };

float ease(int kind, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	switch (kind) {
	case SmoothCurve:
		return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
	case Snappy: // exponential in/out: slow start, fast middle, soft landing
		if (t <= 0.0f || t >= 1.0f)
			return t;
		return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
	case Overshoot: { // ease in/out back: pulls back, overshoots, settles
		const float c1 = 1.70158f, c2 = c1 * 1.525f;
		return t < 0.5f ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f
				: (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
	}
	default:
		return t;
	}
}

struct Data {
	obs_source_t *source = nullptr;
	const Desc *desc = nullptr;
	gs_effect_t *effect = nullptr;
	gs_image_file_t logo = {};
	bool logoLoaded = false;

	int easing = SmoothCurve;
	float intensity = 1.0f;
	float softness = 0.3f;
	vec2 dir = {1.0f, 0.0f};
	vec4 colorA, colorB;
};

vec4 colorFromInt(long long c, float alpha = 1.0f)
{
	// obs_data colours are 0xAABBGGRR in sRGB. The shader draws into an sRGB framebuffer (linear
	// light), so convert or colours come out washed out.
	auto lin = [](float v) { return std::pow(v, 2.2f); };
	vec4 v;
	vec4_set(&v, lin(float(c & 0xFF) / 255.0f), lin(float((c >> 8) & 0xFF) / 255.0f),
		 lin(float((c >> 16) & 0xFF) / 255.0f), alpha);
	return v;
}

int easingFromString(const char *s)
{
	if (!strcmp(s, "linear"))
		return Linear;
	if (!strcmp(s, "snappy"))
		return Snappy;
	if (!strcmp(s, "overshoot"))
		return Overshoot;
	return SmoothCurve;
}

const char *getName(void *typeData)
{
	return static_cast<const Desc *>(typeData)->name;
}

void getDefaults(void *typeData, obs_data_t *s)
{
	const Desc *d = static_cast<const Desc *>(typeData);
	obs_data_set_default_string(s, "easing", "smooth");
	obs_data_set_default_double(s, "intensity", d->intensity);
	obs_data_set_default_string(s, "direction", "left");
	obs_data_set_default_double(s, "softness", 0.3);
	// Flash defaults to white; the brand stinger to violet + pink.
	obs_data_set_default_int(s, "color_a", d->mode == Flash ? 0xFFFFFF : 0xED3A7C);
	obs_data_set_default_int(s, "color_b", 0xFF4899);
	// The brand stinger shows the RareStudio logo unless you point it at your own.
	const QByteArray logo = (QCoreApplication::applicationDirPath() + "/data/rarestudio/logo.png").toUtf8();
	obs_data_set_default_string(s, "logo_path", logo.constData());
}

obs_properties_t *getProperties(void *data)
{
	auto *d = static_cast<Data *>(data);
	obs_properties_t *p = obs_properties_create();

	obs_property_t *e = obs_properties_add_list(p, "easing", "Easing curve", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(e, "Smooth (ease in/out)", "smooth");
	obs_property_list_add_string(e, "Snappy (fast middle, soft landing)", "snappy");
	obs_property_list_add_string(e, "Overshoot (pulls back, then springs)", "overshoot");
	obs_property_list_add_string(e, "Linear", "linear");

	obs_properties_add_float_slider(p, "intensity", "Intensity", 0.1, 2.0, 0.05);
	if (d && d->desc->direction) {
		obs_property_t *dir = obs_properties_add_list(p, "direction", "Direction", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(dir, "Left", "left");
		obs_property_list_add_string(dir, "Right", "right");
		obs_property_list_add_string(dir, "Up", "up");
		obs_property_list_add_string(dir, "Down", "down");
	}
	if (d && d->desc->softness)
		obs_properties_add_float_slider(p, "softness", "Edge softness", 0.0, 1.0, 0.02);
	if (d && d->desc->color) {
		obs_properties_add_color(p, "color_a", d->desc->mode == Flash ? "Flash colour" : "Bar colour 1");
		if (d->desc->mode == BrandStinger)
			obs_properties_add_color(p, "color_b", "Bar colour 2");
	}
	if (d && d->desc->logo)
		obs_properties_add_path(p, "logo_path", "Logo image (PNG with transparency)", OBS_PATH_FILE,
					"Images (*.png *.jpg *.webp *.bmp)", nullptr);
	return p;
}

void loadLogo(Data *d, const char *path)
{
	obs_enter_graphics();
	if (d->logoLoaded) {
		gs_image_file_free(&d->logo);
		d->logoLoaded = false;
	}
	if (path && *path) {
		gs_image_file_init(&d->logo, path);
		gs_image_file_init_texture(&d->logo);
		d->logoLoaded = d->logo.texture != nullptr;
	}
	obs_leave_graphics();
}

void update(void *data, obs_data_t *s)
{
	auto *d = static_cast<Data *>(data);
	d->easing = easingFromString(obs_data_get_string(s, "easing"));
	d->intensity = float(obs_data_get_double(s, "intensity"));
	d->softness = float(obs_data_get_double(s, "softness"));
	const char *dir = obs_data_get_string(s, "direction");
	if (!strcmp(dir, "right"))
		vec2_set(&d->dir, -1.0f, 0.0f);
	else if (!strcmp(dir, "up"))
		vec2_set(&d->dir, 0.0f, 1.0f);
	else if (!strcmp(dir, "down"))
		vec2_set(&d->dir, 0.0f, -1.0f);
	else
		vec2_set(&d->dir, 1.0f, 0.0f);
	d->colorA = colorFromInt(obs_data_get_int(s, "color_a"));
	d->colorB = colorFromInt(obs_data_get_int(s, "color_b"));

	if (d->desc->logo) {
		const char *path = obs_data_get_string(s, "logo_path");
		loadLogo(d, path);
	}
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new Data;
	d->source = source;
	d->desc = static_cast<const Desc *>(obs_source_get_type_data(source));

	obs_enter_graphics();
	char *err = nullptr;
	d->effect = gs_effect_create(kEffect, "rare_transitions.effect", &err);
	obs_leave_graphics();
	if (!d->effect) {
		blog(LOG_ERROR, "RareTransitions: shader failed to compile: %s", err ? err : "(no details)");
		bfree(err);
		delete d;
		return nullptr;
	}
	update(d, settings);
	return d;
}

void destroy(void *data)
{
	auto *d = static_cast<Data *>(data);
	obs_enter_graphics();
	if (d->logoLoaded)
		gs_image_file_free(&d->logo);
	gs_effect_destroy(d->effect);
	obs_leave_graphics();
	delete d;
}

void renderCallback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	auto *d = static_cast<Data *>(data);
	if (!a && !b)
		return;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	auto set = [&](const char *name) { return gs_effect_get_param_by_name(d->effect, name); };
	gs_effect_set_texture_srgb(set("tex_a"), a ? a : b);
	gs_effect_set_texture_srgb(set("tex_b"), b ? b : a);
	gs_texture_t *logoTex = (d->logoLoaded && d->logo.texture) ? d->logo.texture : nullptr;
	gs_effect_set_texture(set("tex_logo"), logoTex ? logoTex : (a ? a : b));
	gs_effect_set_int(set("has_logo"), logoTex ? 1 : 0);
	gs_effect_set_int(set("has_a"), a ? 1 : 0);
	gs_effect_set_int(set("has_b"), b ? 1 : 0);

	vec2 res;
	vec2_set(&res, float(cx), float(cy));
	gs_effect_set_float(set("progress"), ease(d->easing, t)); // may overshoot [0,1] for the "back" curve
	gs_effect_set_float(set("raw"), t);
	gs_effect_set_int(set("mode"), int(d->desc->mode));
	gs_effect_set_float(set("intensity"), d->intensity);
	gs_effect_set_float(set("softness"), d->softness);
	gs_effect_set_vec2(set("dir"), &d->dir);
	gs_effect_set_vec2(set("res"), &res);
	gs_effect_set_vec4(set("color_a"), &d->colorA);
	gs_effect_set_vec4(set("color_b"), &d->colorB);

	while (gs_effect_loop(d->effect, "Transition"))
		gs_draw_sprite(nullptr, 0, cx, cy);

	gs_enable_framebuffer_srgb(previous);
}

void videoRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<Data *>(data);
	obs_transition_video_render(d->source, renderCallback);
}

float mixA(void *data, float t)
{
	return 1.0f - std::clamp(ease(static_cast<Data *>(data)->easing, t), 0.0f, 1.0f);
}

float mixB(void *data, float t)
{
	return std::clamp(ease(static_cast<Data *>(data)->easing, t), 0.0f, 1.0f);
}

bool audioRender(void *data, uint64_t *ts, obs_source_audio_mix *audio, uint32_t mixers, size_t channels, size_t rate)
{
	return obs_transition_audio_render(static_cast<Data *>(data)->source, ts, audio, mixers, channels, rate, mixA, mixB);
}

enum gs_color_space colorSpace(void *data, size_t, const enum gs_color_space *)
{
	return obs_transition_video_get_color_space(static_cast<Data *>(data)->source);
}


// ---- Background blur filter -------------------------------------------------------------------
// Used behind the 9:16 Shorts layout: a spiral-sampled blur in one pass, so it stays cheap.
const char *kBlurEffect = R"FX(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 texel;
uniform float radius;

sampler_state samp {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertData {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertData VSDefault(VertData v_in)
{
	VertData v;
	v.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
	v.uv  = v_in.uv;
	return v;
}

float4 PSBlur(VertData v_in) : TARGET
{
	float4 acc = float4(0.0, 0.0, 0.0, 0.0);
	float total = 0.0;
	for (int i = 0; i < 28; i++) {
		float a = float(i) * 2.39996;
		float rr = sqrt((float(i) + 0.5) / 28.0);
		float2 o = float2(cos(a), sin(a)) * rr * radius * texel;
		float w = 1.0 - rr * 0.55;
		acc += image.Sample(samp, v_in.uv + o) * w;
		total += w;
	}
	return acc / total;
}

technique Draw
{
	pass
	{
		vertex_shader = VSDefault(v_in);
		pixel_shader  = PSBlur(v_in);
	}
}
)FX";

struct BlurData {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	float radius = 28.0f;
};

const char *blurName(void *) { return "RareStudio Background Blur"; }

void blurDefaults(obs_data_t *s) { obs_data_set_default_double(s, "radius", 28.0); }

void blurUpdate(void *data, obs_data_t *s) { static_cast<BlurData *>(data)->radius = float(obs_data_get_double(s, "radius")); }

obs_properties_t *blurProps(void *)
{
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_float_slider(p, "radius", "Blur radius (pixels)", 1.0, 120.0, 1.0);
	return p;
}

void *blurCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *d = new BlurData;
	d->source = source;
	obs_enter_graphics();
	char *err = nullptr;
	d->effect = gs_effect_create(kBlurEffect, "rare_blur.effect", &err);
	obs_leave_graphics();
	if (!d->effect) {
		blog(LOG_ERROR, "RareStudio blur filter failed to compile: %s", err ? err : "(no details)");
		bfree(err);
		delete d;
		return nullptr;
	}
	blurUpdate(d, settings);
	return d;
}

void blurDestroy(void *data)
{
	auto *d = static_cast<BlurData *>(data);
	obs_enter_graphics();
	gs_effect_destroy(d->effect);
	obs_leave_graphics();
	delete d;
}

void blurRender(void *data, gs_effect_t *)
{
	auto *d = static_cast<BlurData *>(data);
	obs_source_t *target = obs_filter_get_target(d->source);
	const uint32_t w = obs_source_get_base_width(target), h = obs_source_get_base_height(target);
	if (!w || !h) {
		obs_source_skip_video_filter(d->source);
		return;
	}
	// A blur samples its neighbours, so the picture must be rendered to a texture first.
	if (!obs_source_process_filter_begin(d->source, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;
	vec2 texel;
	vec2_set(&texel, 1.0f / w, 1.0f / h);
	gs_effect_set_vec2(gs_effect_get_param_by_name(d->effect, "texel"), &texel);
	gs_effect_set_float(gs_effect_get_param_by_name(d->effect, "radius"), d->radius);
	obs_source_process_filter_end(d->source, d->effect, w, h);
}
} // namespace

void registerAll()
{
	static obs_source_info blur = {};
	blur.id = "rare_bg_blur";
	blur.type = OBS_SOURCE_TYPE_FILTER;
	blur.output_flags = OBS_SOURCE_VIDEO;
	blur.get_name = blurName;
	blur.create = blurCreate;
	blur.destroy = blurDestroy;
	blur.update = blurUpdate;
	blur.get_defaults = blurDefaults;
	blur.get_properties = blurProps;
	blur.video_render = blurRender;
	obs_register_source(&blur);

	static obs_source_info infos[kCount];
	for (int i = 0; i < kCount; ++i) {
		obs_source_info &info = infos[i];
		info = {};
		info.id = kDescs[i].id;
		info.type = OBS_SOURCE_TYPE_TRANSITION;
		info.type_data = const_cast<Desc *>(&kDescs[i]);
		info.get_name = getName;
		info.create = create;
		info.destroy = destroy;
		info.update = update;
		info.video_render = videoRender;
		info.audio_render = audioRender;
		info.get_properties = getProperties;
		info.get_defaults2 = getDefaults;
		info.video_get_color_space = colorSpace;
		obs_register_source(&info);
	}
}

QList<Info> list()
{
	QList<Info> out;
	for (const Desc &d : kDescs)
		out.append(Info{QString::fromUtf8(d.id), QString::fromUtf8(d.name)});
	return out;
}

} // namespace RareTransitions
