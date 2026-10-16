// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/effects/radial_animation.h"

#include "base/options.h"
#include "ui/arc_angles.h"
#include "ui/painter.h"
#include "styles/style_widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <QtCore/QLineF>
#include <QtGui/QPainterPath>

namespace Ui {
namespace {

constexpr auto kFullArcLength = arc::kFullLength;
constexpr auto kMd3Cycle = crl::time(6000);
constexpr auto kOptionMd3LoadingIndicators = "xenon-md3-loading-indicators";

base::options::toggle OptionMd3LoadingIndicators({
	.id = kOptionMd3LoadingIndicators,
	.name = "MD3 loading indicators",
});

base::options::toggle OptionMd3LoadingWaves({
	.id = "xenon-md3-loading-waves",
	.name = "MD3 loading indicator waves",
});

void DrawWavyRadial(
		QPainter &p,
		const QRectF &rect,
		const RadialState &state) {
	constexpr auto kSteps = 180;
	constexpr auto kWaves = 11;
	const auto radius = std::min(rect.width(), rect.height()) / 2.;
	const auto amplitude = radius * 0.05 * state.waveAmplitude;
	const auto phase = (crl::now() % 11000) / 11000.;
	auto points = std::array<QPointF, kSteps + 1>();
	auto distances = std::array<float64, kSteps + 1>();
	for (auto i = 0; i <= kSteps; ++i) {
		const auto angle = i * 2. * std::numbers::pi / kSteps;
		const auto r = radius + amplitude * std::sin(kWaves * angle);
		points[i] = QPointF(r * std::cos(angle), r * std::sin(angle));
		if (i) {
			distances[i] = distances[i - 1]
				+ QLineF(points[i - 1], points[i]).length();
		}
	}
	const auto perimeter = distances.back();
	if (perimeter <= 0.) {
		return;
	}
	const auto start = phase * perimeter;
	const auto end = start + std::abs(state.arcLength)
		* perimeter / kFullArcLength;
	auto path = QPainterPath();
	const auto pointAt = [&](float64 distance) {
		distance = std::fmod(distance, perimeter);
		const auto next = std::clamp(int(std::upper_bound(
			distances.begin(), distances.end(), distance)
			- distances.begin()), 1, kSteps);
		const auto fraction = (distance - distances[next - 1])
			/ (distances[next] - distances[next - 1]);
		return points[next - 1] * (1. - fraction) + points[next] * fraction;
	};
	path.moveTo(pointAt(start));
	for (auto cycle = 0; cycle != 2; ++cycle) {
		for (auto i = 1; i <= kSteps; ++i) {
			const auto distance = distances[i] + cycle * perimeter;
			if (distance > start && distance < end) {
				path.lineTo(points[i]);
			}
		}
	}
	path.lineTo(pointAt(end));
	p.save();
	p.translate(rect.center());
	p.rotate(-(state.arcFrom + state.arcLength) / 16. - phase * 360.);
	p.setBrush(Qt::NoBrush);
	p.drawPath(path);
	p.restore();
}

float64 MaterialEasing(
		float64 fraction,
		float64 x1,
		float64 y1,
		float64 x2,
		float64 y2) {
	fraction = std::clamp(fraction, 0., 1.);
	if (fraction == 0. || fraction == 1.) {
		return fraction;
	}
	auto low = 0.;
	auto high = 1.;
	for (auto i = 0; i != 24; ++i) {
		const auto t = (low + high) / 2.;
		const auto u = 1. - t;
		const auto x = 3. * u * u * t * x1
			+ 3. * u * t * t * x2 + t * t * t;
		if (x < fraction) {
			low = t;
		} else {
			high = t;
		}
	}
	const auto t = (low + high) / 2.;
	const auto u = 1. - t;
	return 3. * u * u * t * y1 + 3. * u * t * t * y2 + t * t * t;
}

RadialState MaterialLoadingState(crl::time elapsed, float64 shown) {
	const auto time = std::max(elapsed, crl::time(0)) % kMd3Cycle;
	const auto rotationTime = time % 1500;
	const auto rotation = 1080. * time / kMd3Cycle
		+ 90. * (time / 1500)
		+ 90. * std::min(rotationTime / 300., 1.)
		+ 90.;
	const auto growth = (time <= 3000)
		? time / 3000.
		: 1. - MaterialEasing((time - 3000) / 3000., 0.2, 0., 0., 1.);
	const auto length = int(base::SafeRound((36. + 277.2 * growth) * 16.));
	return {
		.shown = shown,
		.arcFrom = int(base::SafeRound(-rotation * 16.)) - length,
		.arcLength = length,
		.material = true,
		.waveAmplitude = (time < 3000)
			? MaterialEasing(time / 500., 0.2, 0., 0., 1.)
			: 1. - MaterialEasing((time - 3000) / 500., 0.3, 0., 0.8, 0.15),
	};
}

} // namespace

const int RadialState::kFull = kFullArcLength;

void DrawRadial(
		QPainter &p,
		const QRectF &rect,
		const RadialState &state) {
	PainterHighQualityEnabler hq(p);
	const auto previous = p.pen();
	if (state.material && OptionMd3LoadingIndicators.value()) {
		auto pen = previous;
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		const auto sweep = std::abs(state.arcLength) / 16.;
		auto gap = 22.;
		if (state.progress >= 0.) {
			gap = std::clamp(22. * (325. - sweep) / 12., 0., 22.);
			if (state.progress < 0.01 + 12. / 360.) {
				const auto t = std::clamp(
					(state.progress - 0.01) / (12. / 360.), 0., 1.);
				gap = 22. * t * t * (3. - 2. * t);
			}
		}
		const auto track = 360. - sweep - 2. * gap;
		if (track > 0.) {
			const auto opacity = p.opacity();
			p.setOpacity(opacity * 0.4);
			p.drawArc(
				rect,
				state.arcFrom + state.arcLength + int(base::SafeRound(gap * 16.)),
				int(base::SafeRound(track * 16.)));
			p.setOpacity(opacity);
		}
	}
	if (state.material
		&& OptionMd3LoadingIndicators.value()
		&& OptionMd3LoadingWaves.value()
		&& state.waveAmplitude > 0.
		&& !anim::Disabled()) {
		DrawWavyRadial(p, rect, state);
	} else {
		p.drawArc(rect, state.arcFrom, state.arcLength);
	}
	p.setPen(previous);
}

bool DrawMaterialLoading(
		QPainter &p,
		const QRectF &rect,
		crl::time elapsed) {
	if (!OptionMd3LoadingIndicators.value() || anim::Disabled()) {
		return false;
	}
	DrawRadial(p, rect, MaterialLoadingState(elapsed, 1.));
	return true;
}

void RadialAnimation::start(float64 prg) {
	_firstStart = _lastStart = _lastTime = crl::now();
	_wave = {};
	const auto safe = std::max(prg, 0.0001) * arc::kAlmostFullLength;
	const auto iprg = int(base::SafeRound(safe));
	const auto strict = prg * arc::kAlmostFullLength;
	const auto iprgstrict = int(base::SafeRound(strict));
	_arcEnd = anim::value(iprgstrict, iprg);
	_animation.start();
}

bool RadialAnimation::update(float64 prg, bool finished, crl::time ms) {
	const auto safe = std::max(prg, 0.0001) * arc::kAlmostFullLength;
	const auto iprg = int(base::SafeRound(safe));
	const auto result = (iprg != int(base::SafeRound(_arcEnd.to())))
		|| (_finished != finished);
	if (_finished != finished) {
		_arcEnd.start(iprg);
		_finished = finished;
		_lastStart = _lastTime;
	} else if (result) {
		_arcEnd.start(iprg);
		_lastStart = _lastTime;
	}
	_lastTime = ms;

	const auto dt = float64(ms - _lastStart);
	const auto fulldt = float64(ms - _firstStart);
	const auto opacitydt = _finished
		? (_lastStart - _firstStart)
		: fulldt;
	_opacity = std::min(opacitydt / st::radialDuration, 1.);
	if (anim::Disabled()) {
		_arcEnd.update(1., anim::linear);
		if (finished) {
			stop();
		}
	} else if (!finished) {
		_arcEnd.update(1. - (st::radialDuration / (st::radialDuration + dt)), anim::linear);
	} else if (dt >= st::radialDuration) {
		_arcEnd.update(1., anim::linear);
		stop();
	} else {
		auto r = dt / st::radialDuration;
		_arcEnd.update(r, anim::linear);
		_opacity *= 1 - r;
	}
	auto fromstart = fulldt / st::radialPeriod;
	_arcStart.update(fromstart - std::floor(fromstart), anim::linear);
	const auto current = _arcEnd.current() / arc::kAlmostFullLength;
	const auto amplitude = (prg < 0.01 || (current > 0.1 && current < 0.95))
		? 1.
		: 0.;
	if (amplitude != _wave.target) {
		_wave.from = _wave.current;
		_wave.target = amplitude;
		_wave.started = ms;
	}
	const auto fraction = (ms - _wave.started) / 500.;
	const auto easing = (_wave.target > _wave.from)
		? MaterialEasing(fraction, 0.2, 0., 0., 1.)
		: MaterialEasing(fraction, 0.3, 0., 0.8, 0.15);
	_wave.current = _wave.from + (_wave.target - _wave.from) * easing;
	return result;
}

void RadialAnimation::stop() {
	_firstStart = _lastStart = _lastTime = 0;
	_arcEnd = anim::value();
	_animation.stop();
}

void RadialAnimation::draw(
		QPainter &p,
		const QRectF &inner,
		float64 thickness,
		style::color color) const {
	const auto state = computeState();

	auto o = p.opacity();
	p.setOpacity(o * state.shown);

	auto pen = color->p;
	auto was = p.pen();
	pen.setWidthF(thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);

	{
		PainterHighQualityEnabler hq(p);
		DrawRadial(p, inner, state);
	}

	p.setPen(was);
	p.setOpacity(o);
}

RadialState RadialAnimation::computeState() const {
	if (OptionMd3LoadingIndicators.value() && !anim::Disabled()) {
		const auto progress = _arcEnd.to() / arc::kAlmostFullLength;
		if (progress < 0.01 && !_finished) {
			auto state = MaterialLoadingState(_lastTime - _firstStart, _opacity);
			state.waveAmplitude *= _wave.current;
			return state;
		}
		const auto current = _arcEnd.current() / arc::kAlmostFullLength;
		const auto length = int(base::SafeRound(
			std::clamp(current * kFullArcLength, 64., float64(kFullArcLength))));
		return {
			.shown = _opacity,
			.arcFrom = arc::kQuarterLength - length,
			.arcLength = length,
			.material = true,
			.progress = current,
			.waveAmplitude = _wave.current,
		};
	}
	auto length = arc::kMinLength + int(base::SafeRound(_arcEnd.current()));
	auto from = arc::kQuarterLength
		- length
		- (anim::Disabled() ? 0 : int(base::SafeRound(_arcStart.current())));
	if (style::RightToLeft()) {
		from = arc::kQuarterLength - (from - arc::kQuarterLength) - length;
		if (from < 0) from += arc::kFullLength;
	}
	return { _opacity, from, length };
}

void InfiniteRadialAnimation::init() {
	anim::Disables() | rpl::filter([=] {
		return animating();
	}) | rpl::on_next([=](bool disabled) {
		if (!disabled && !_animation.animating()) {
			_animation.start();
		} else if (disabled && _animation.animating()) {
			_animation.stop();
		}
	}, _lifetime);
}

void InfiniteRadialAnimation::start(crl::time skip) {
	if (!animating()) {
		const auto now = crl::now();
		_workStarted = std::max(now + _st.sineDuration - skip, crl::time(1));
		_workFinished = 0;
		_fade = {};
	}
	if (!anim::Disabled() && !_animation.animating()) {
		_animation.start();
	}
}

void InfiniteRadialAnimation::stop(anim::type animated) {
	const auto now = crl::now();
	_fade = {};
	if (anim::Disabled() || animated == anim::type::instant) {
		_workFinished = now;
	}
	if (!_workFinished) {
		const auto zero = _workStarted - _st.sineDuration;
		const auto index = (now - zero + _st.sinePeriod - _st.sineShift)
			/ _st.sinePeriod;
		_workFinished = zero
			+ _st.sineShift
			+ (index * _st.sinePeriod)
			+ _st.sineDuration;
	} else if (_workFinished <= now) {
		_animation.stop();
	}
}

void InfiniteRadialAnimation::stopWithFade() {
	if (!animating() || _fade.started) {
		return;
	}
	const auto state = computeState();
	_fade.started = crl::now();
	_fade.fromLength = state.arcLength;
	_fade.fromPosition = state.arcFrom;
	_workFinished = 0;
	if (!anim::Disabled() && !_animation.animating()) {
		_animation.start();
	}
}

void InfiniteRadialAnimation::draw(
		QPainter &p,
		QPoint position,
		int outerWidth) {
	Draw(
		p,
		computeState(),
		position,
		_st.size,
		outerWidth,
		_st.color,
		_st.thickness);
}

void InfiniteRadialAnimation::draw(
		QPainter &p,
		QPoint position,
		QSize size,
		int outerWidth) {
	Draw(
		p,
		computeState(),
		position,
		size,
		outerWidth,
		_st.color,
		_st.thickness);
}

void InfiniteRadialAnimation::Draw(
		QPainter &p,
		const RadialState &state,
		QPoint position,
		QSize size,
		int outerWidth,
		QPen pen,
		int thickness) {
	auto o = p.opacity();
	p.setOpacity(o * state.shown);

	const auto rect = style::rtlrect(
		position.x(),
		position.y(),
		size.width(),
		size.height(),
		outerWidth);
	const auto was = p.pen();
	const auto brush = p.brush();
	if (anim::Disabled()) {
		anim::DrawStaticLoading(p, rect, thickness, pen);
	} else {
		pen.setWidth(thickness);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);

		{
			PainterHighQualityEnabler hq(p);
			DrawRadial(p, rect, state);
		}
	}
	p.setPen(was);
	p.setBrush(brush);
	p.setOpacity(o);
}

RadialState InfiniteRadialAnimation::computeState() {
	auto state = computeLegacyState();
	if (!OptionMd3LoadingIndicators.value()
		|| anim::Disabled()
		|| state.shown <= 0.) {
		return state;
	} else if (_fade.started) {
		state.material = true;
		state.waveAmplitude = MaterialLoadingState(
			crl::now() - (_workStarted - _st.sineDuration), 1.).waveAmplitude;
		return state;
	}
	return MaterialLoadingState(
		crl::now() - (_workStarted - _st.sineDuration), state.shown);
}

RadialState InfiniteRadialAnimation::computeLegacyState() {
	const auto now = crl::now();
	const auto linear = kFullArcLength
		- int(((now * kFullArcLength) / _st.linearPeriod) % kFullArcLength);

	if (_fade.started) {
		const auto fadeDuration = _st.sineDuration;
		const auto elapsed = now - _fade.started;
		if (elapsed >= fadeDuration) {
			_animation.stop();
			return { 0., _fade.fromPosition, 0 };
		}
		const auto progress = elapsed / float64(fadeDuration);
		const auto length = anim::interpolate(
			_fade.fromLength,
			0,
			anim::easeOutQuint(1., progress));
		return { 1. - progress, _fade.fromPosition, length };
	}

	if (!animating()) {
		const auto shown = 0.;
		_animation.stop();
		return {
			shown,
			linear,
			kFullArcLength };
	}
	if (anim::Disabled()) {
		return { 1., 0, kFullArcLength };
	}
	const auto min = int(base::SafeRound(kFullArcLength * _st.arcMin));
	const auto max = int(base::SafeRound(kFullArcLength * _st.arcMax));
	if (now <= _workStarted) {
		// zero .. _workStarted
		const auto zero = _workStarted - _st.sineDuration;
		const auto shown = (now - zero) / float64(_st.sineDuration);
		const auto length = anim::interpolate(
			kFullArcLength,
			min,
			anim::sineInOut(1., std::clamp(shown, 0., 1.)));
		return {
			shown,
			linear,
			length };
	} else if (!_workFinished || now <= _workFinished - _st.sineDuration) {
		// _workStared .. _workFinished - _st.sineDuration
		const auto shown = 1.;
		const auto cycles = (now - _workStarted) / _st.sinePeriod;
		const auto relative = (now - _workStarted) % _st.sinePeriod;
		const auto smallDuration = _st.sineShift - _st.sineDuration;
		const auto basic = int((linear
			+ min
			+ (cycles * (kFullArcLength + min - max))) % kFullArcLength);
		if (relative <= smallDuration) {
			// localZero .. growStart
			return {
				shown,
				basic - min,
				min };
		} else if (relative <= smallDuration + _st.sineDuration) {
			// growStart .. growEnd
			const auto growLinear = (relative - smallDuration) /
				float64(_st.sineDuration);
			const auto growProgress = anim::sineInOut(1., growLinear);
			const auto length = anim::interpolate(min, max, growProgress);
			return {
				shown,
				basic - length,
				length };
		} else if (relative <= _st.sinePeriod - _st.sineDuration) {
			// growEnd .. shrinkStart
			return {
				shown,
				basic - max,
				max };
		} else {
			// shrinkStart .. shrinkEnd
			const auto shrinkLinear = (relative
				- (_st.sinePeriod - _st.sineDuration))
					/ float64(_st.sineDuration);
			const auto shrinkProgress = anim::sineInOut(1., shrinkLinear);
			const auto shrink = anim::interpolate(
				0,
				max - min,
				shrinkProgress);
			return {
				shown,
				basic - max,
				max - shrink }; // interpolate(max, min, shrinkProgress)
		}
	} else {
		// _workFinished - _st.sineDuration .. _workFinished
		const auto hidden = (now - (_workFinished - _st.sineDuration))
			/ float64(_st.sineDuration);
		const auto cycles = (_workFinished - _workStarted) / _st.sinePeriod;
		const auto basic = int((linear
			+ min
			+ cycles * (kFullArcLength + min - max)) % kFullArcLength);
		const auto length = anim::interpolate(
			min,
			kFullArcLength,
			anim::sineInOut(1., std::clamp(hidden, 0., 1.)));
		return {
			1. - hidden,
			basic - length,
			length };
	}
	//const auto frontPeriods = time / st.sinePeriod;
	//const auto frontCurrent = time % st.sinePeriod;
	//const auto frontProgress = anim::sineInOut(
	//	st.arcMax - st.arcMin,
	//	std::min(frontCurrent, crl::time(st.sineDuration))
	//	/ float64(st.sineDuration));
	//const auto backTime = std::max(time - st.sineShift, 0LL);
	//const auto backPeriods = backTime / st.sinePeriod;
	//const auto backCurrent = backTime % st.sinePeriod;
	//const auto backProgress = anim::sineInOut(
	//	st.arcMax - st.arcMin,
	//	std::min(backCurrent, crl::time(st.sineDuration))
	//	/ float64(st.sineDuration));
	//const auto front = linear + base::SafeRound((st.arcMin + frontProgress + frontPeriods * (st.arcMax - st.arcMin)) * kFullArcLength);
	//const auto from = linear + base::SafeRound((backProgress + backPeriods * (st.arcMax - st.arcMin)) * kFullArcLength);
	//const auto length = (front - from);

	//return {
	//	_opacity,
	//	from,
	//	length
	//};
}

} // namespace Ui
