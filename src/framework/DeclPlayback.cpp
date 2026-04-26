// DeclPlayback.cpp
//



rvDeclPlayback::rvDeclPlayback() {
	FreeData();
}

rvDeclPlayback::~rvDeclPlayback() {

}

template< class type >
static float PlaybackClampedCurveTime( const idCurve_UniformCubicBSpline<type> &curve, float localTime ) {
	const int numValues = curve.GetNumValues();
	if ( numValues <= 0 ) {
		return 0.0f;
	}

	const float firstTime = curve.GetTime( 0 );
	const float lastTime = curve.GetTime( numValues - 1 );
	return idMath::ClampFloat( firstTime, lastTime, localTime );
}

static int PlaybackRequestedControl( int control ) {
	if ( control < 0 ) {
		return PBFL_GET_POSITION | PBFL_GET_ANGLES | PBFL_GET_BUTTONS | PBFL_GET_VELOCITY | PBFL_GET_ACCELERATION | PBFL_GET_ANGLES_FROM_VEL;
	}
	return control;
}

/*
=====================
rvDeclPlayback::ParseSample
=====================
*/
void rvDeclPlayback::ParseSample(idLexer* src, idVec3& pos, idAngles& ang)
{
	idToken token; 

	while ( src->ReadToken( &token ) ) {
		if (token == "}")
		{
			break;
		}
		else if (token == "down" || token == "up" || token == "impulse")
		{
			src->ParseInt(); // jmarshall: decompiled code doesn't use this, seems like its just skipped.
			continue;
		}
		else if (token == "rotate")
		{
			ang.pitch = src->ParseFloat();
			src->ExpectTokenString(",");
			ang.yaw = src->ParseFloat();
			src->ExpectTokenString(",");
			ang.roll = src->ParseFloat();
			flags |= PBFL_GET_ANGLES;
			continue;
		}
		else if (token == "ang")
		{
			ang.pitch = src->ParseFloat();
			src->ExpectTokenString(",");
			ang.yaw = src->ParseFloat();
			ang.roll = 0.0;
			flags |= PBFL_GET_ANGLES;
			continue;
		}
		else if (token == "pos")
		{
			pos.x = src->ParseFloat();
			src->ExpectTokenString(",");
			pos.y = src->ParseFloat();
			src->ExpectTokenString(",");
			pos.z = src->ParseFloat();
			flags |= PBFL_GET_POSITION;
			continue;
		}
		else
		{
			src->Error("rvDeclPlayback::ParseSample: Invalid or unexpected token %s\n", token.c_str());
			return;
		}
	}
}

/*
=====================
rvDeclPlayback::ParseData
=====================
*/
bool rvDeclPlayback::ParseData(idLexer* src) {
	idToken token;
	idVec3 pos;
	idAngles ang;

	float t = 0;
	pos.Zero();
	ang.Zero();

	if ( !src->ExpectTokenString( "{" ) ) {
		return false;
	}

	while ( src->ReadToken(&token) )
	{

		if (token == "}")
		{
			duration = t;
			return true;
		}

		if (token == "{")
		{
			ParseSample(src, pos, ang);

			points.AddValue(t, pos);
			angles.AddValue(t, ang);
			t += frameRate > 0.0f ? 1.0f / frameRate : 1.0f / 15.0f;
			continue;
		}
		else
		{
			src->Error("rvDeclPlayback::ParseData: Invalid or unexpected token %s\n", token.c_str());
			return false;
		}
	}

	return false;
}

/*
=====================
rvDeclPlayback::ParseButton
=====================
*/
void rvDeclPlayback::ParseButton(idLexer* src, byte& button, rvButtonState& state) {
	idToken token;
	byte impulse = 0;

	state.time = src->ParseFloat();

	while ( src->ReadToken(&token) )
	{

		if (token == "}")
		{
			break;
		}

		if (token == "impulse")
		{
			impulse = src->ParseInt();
			continue;
		}
		else if (token == "up")
		{
			button = ~src->ParseInt() & button;
			continue;
		}
		else if (token == "down")
		{
			button = src->ParseInt() | button;
			continue;
		}
	}

	state.state = button;
	state.impulse = impulse;
}

/*
=====================
rvDeclPlayback::ParseButtons
=====================
*/
bool rvDeclPlayback::ParseButtons(idLexer* src) {
	idToken token;
	byte button = 0;
	rvButtonState state;

	if ( !src->ExpectTokenString( "{" ) ) {
		return false;
	}

	while (true)
	{
		if (!src->ReadToken(&token)) {
			return false;
		}

		if (token == "}")
		{
			return true;
		}

		if (token == "{")
		{
			ParseButton(src, button, state);
			flags |= PBFL_GET_BUTTONS;
			buttons.Append(state);
			continue;
		}

		src->Error("rvDeclPlayback::ParseButtons: Invalid or unexpected token %s\n", token.c_str());
		return false;
	}
}


/*
=====================
rvDeclPlayback::ParseSequence
=====================
*/
bool rvDeclPlayback::ParseSequence(idLexer* src) {
	src->ExpectTokenString("sequence");
	src->ExpectTokenString("{");

	idToken token;

	while ( src->ReadToken(&token) )
	{

		if (token == "}")
		{
			return true;
		}

		if (token == "framerate")
		{
			frameRate = src->ParseFloat();
			continue;
		}
		else if (token == "origin")
		{
			origin.x = src->ParseFloat();
			src->ExpectTokenString(",");
			origin.y = src->ParseFloat();
			src->ExpectTokenString(",");
			origin.z = src->ParseFloat();
			continue;
		}
		else if (token == "destination")
		{
			idStr dest;
			src->ParseRestOfLine(dest);
			continue;
		}
		else
		{
			src->Error("rvDeclPlayback::ParseSequence: Invalid or unexpected token %s\n", token.c_str());
			return false;
		}
	}

	return false;
}

/*
=====================
rvDeclPlayback::DefaultDefinition
=====================
*/
const char* rvDeclPlayback::DefaultDefinition() const
{
	return "{ sequence { } data { } }";
}

/*
=====================
rvDeclPlayback::Size
=====================
*/
size_t rvDeclPlayback::Size(void) const {
	return sizeof(rvDeclPlayback)
		+ buttons.Allocated()
		+ points.GetNumValues() * sizeof(idVec3)
		+ angles.GetNumValues() * sizeof(idAngles);
}

/*
=====================
rvDeclPlayback::Copy
=====================
*/
void rvDeclPlayback::Copy(rvDeclPlayback* pb) {
	if (pb == NULL) {
		FreeData();
		return;
	}

	flags = pb->flags & ~PBFL_ED_MASK;
	frameRate = pb->frameRate;
	duration = pb->duration;
	origin = pb->origin;
	bounds = pb->bounds;
	points = pb->points;
	angles = pb->angles;
	buttons = pb->buttons;
}

/*
=====================
rvDeclPlayback::SetOrigin
=====================
*/
void rvDeclPlayback::SetOrigin(void) {
	const int numPoints = points.GetNumValues();
	if (numPoints <= 0) {
		return;
	}

	const idVec3 offset = points.GetValue(0);
	origin += offset;
	bounds.Clear();

	for (int i = 0; i < numPoints; ++i) {
		idVec3 relativePoint = points.GetValue(i) - offset;
		points.SetValue(i, relativePoint);
		bounds.AddPoint(relativePoint);
	}
}

/*
=====================
rvDeclPlayback::Start
=====================
*/
void rvDeclPlayback::Start(void) {
	origin.Zero();
	bounds.Clear();
	points.Clear();
	angles.Clear();
	buttons.Clear();
	flags = 0;
	duration = 0.0f;
	points.SetGranularity(60);
	angles.SetGranularity(60);
	buttons.SetGranularity(60);
}

/*
=====================
rvDeclPlayback::Finish
=====================
*/
bool rvDeclPlayback::Finish(float desiredDuration) {
	SetOrigin();

	if (desiredDuration < 0.0f || duration <= 0.0f || desiredDuration == duration) {
		return true;
	}

	rvDeclPlayback temp;
	temp.Copy(this);

	const float sourceDuration = temp.duration;
	const float speed = desiredDuration > 0.0f ? sourceDuration / desiredDuration : 1.0f;
	const float frameStep = frameRate > 0.0f ? 1.0f / frameRate : 1.0f / 15.0f;
	const int savedFlags = flags;
	const float savedFrameRate = frameRate;
	const idVec3 savedOrigin = origin;

	points.Clear();
	angles.Clear();
	buttons.Clear();
	flags = savedFlags;
	frameRate = savedFrameRate;
	origin = savedOrigin;
	duration = desiredDuration;
	bounds.Clear();

	byte previousState = 0;
	for (int i = 0; i < temp.buttons.Num(); ++i) {
		rvButtonState state = temp.buttons[i];
		if (state.state == previousState && state.impulse == 0) {
			continue;
		}
		state.time = speed > 0.0f ? state.time / speed : 0.0f;
		buttons.Append(state);
		previousState = state.state;
	}

	for (float outputTime = 0.0f; outputTime <= desiredDuration; outputTime += frameStep) {
		rvDeclPlaybackData pbd;
		pbd.Init();

		const float sourceTime = speed * outputTime;
		temp.GetCurrentData(PBFL_GET_POSITION | PBFL_GET_ANGLES, sourceTime, sourceTime, &pbd);

		points.AddValue(outputTime, pbd.GetPosition() - origin);
		angles.AddValue(outputTime, pbd.GetAngles());
		bounds.AddPoint(pbd.GetPosition() - origin);
	}

	return true;
}

/*
=====================
rvDeclPlayback::SetCurrentData
=====================
*/
bool rvDeclPlayback::SetCurrentData(float localTime, int control, rvDeclPlaybackData* pbd) {
	if (pbd == NULL) {
		return false;
	}

	control = PlaybackRequestedControl(control);
	if (control & PBFL_GET_POSITION) {
		points.AddValue(localTime, pbd->GetPosition());
		flags |= PBFL_GET_POSITION;
		bounds.AddPoint(pbd->GetPosition());
	}

	if (control & PBFL_GET_ANGLES) {
		angles.AddValue(localTime, pbd->GetAngles());
		flags |= PBFL_GET_ANGLES;
	}

	if (control & PBFL_GET_BUTTONS) {
		rvButtonState state;
		state.Init(localTime, pbd->GetButtons(), pbd->GetImpulse());
		buttons.Append(state);
		flags |= PBFL_GET_BUTTONS;
	}

	if (localTime > duration) {
		duration = localTime;
	}

	return true;
}

/*
=====================
rvDeclPlayback::GetCurrentOffset
=====================
*/
bool rvDeclPlayback::GetCurrentOffset(float localTime, idVec3& pos) const {
	if (points.GetNumValues() <= 0) {
		pos.Zero();
		return false;
	}

	pos = points.GetCurrentValue(PlaybackClampedCurveTime(points, localTime));
	return true;
}

/*
=====================
rvDeclPlayback::GetCurrentAngles
=====================
*/
bool rvDeclPlayback::GetCurrentAngles(float localTime, idAngles& ang) const {
	if (angles.GetNumValues() <= 0) {
		ang.Zero();
		return false;
	}

	ang = angles.GetCurrentValue(PlaybackClampedCurveTime(angles, localTime));
	return true;
}

/*
=====================
rvDeclPlayback::GetCurrentData
=====================
*/
bool rvDeclPlayback::GetCurrentData(int control, float localTime, float lastTime, rvDeclPlaybackData* pbd) const {
	if (pbd == NULL) {
		return true;
	}

	control = PlaybackRequestedControl(control);
	const bool hasPoints = (flags & PBFL_GET_POSITION) != 0 && points.GetNumValues() > 0;
	const bool hasAngles = (flags & PBFL_GET_ANGLES) != 0 && angles.GetNumValues() > 0;
	const bool hasButtons = (flags & PBFL_GET_BUTTONS) != 0 && buttons.Num() > 0;
	const bool expired = duration <= 0.0f || localTime >= duration;

	pbd->SetChanged(0);
	pbd->SetImpulse(0);

	idVec3 offset;
	offset.Zero();
	if (hasPoints && GetCurrentOffset(localTime, offset)) {
		if (control & PBFL_GET_POSITION) {
			pbd->SetPosition(origin + offset);
		}
		if (control & PBFL_GET_VELOCITY) {
			pbd->SetVelocity(points.GetCurrentFirstDerivative(PlaybackClampedCurveTime(points, localTime)));
		}
		if (control & PBFL_GET_ACCELERATION) {
			pbd->SetAcceleration(points.GetCurrentSecondDerivative(PlaybackClampedCurveTime(points, localTime)));
		}
		if (control & PBFL_GET_ANGLES_FROM_VEL) {
			const idVec3 velocity = points.GetCurrentFirstDerivative(PlaybackClampedCurveTime(points, localTime));
			pbd->SetVelocity(velocity);
			if (velocity.LengthSqr() > Square(1.0e-6f)) {
				pbd->SetAngles(velocity.ToAngles());
			} else if (hasAngles) {
				idAngles ang;
				GetCurrentAngles(localTime, ang);
				pbd->SetAngles(ang);
			}
		}
	}

	if ((control & PBFL_GET_ANGLES_FROM_VEL) && !hasPoints && hasAngles) {
		idAngles ang;
		GetCurrentAngles(localTime, ang);
		pbd->SetAngles(ang);
	}

	if ((control & PBFL_GET_ANGLES) && hasAngles) {
		idAngles ang;
		GetCurrentAngles(localTime, ang);
		pbd->SetAngles(ang);
	}

	if ((control & PBFL_GET_BUTTONS) && hasButtons) {
		const bool includeStart = lastTime <= 0.0f;
		const float eventStart = lastTime < 0.0f ? 0.0f : (lastTime > localTime ? localTime : lastTime);
		byte previousButtons = 0;
		byte currentButtons = 0;
		byte changedButtons = 0;
		byte impulse = 0;

		for (int i = 0; i < buttons.Num(); ++i) {
			const rvButtonState& state = buttons[i];
			const byte changed = previousButtons ^ state.state;

			if (state.time <= localTime) {
				currentButtons = state.state;
			}

			if ((includeStart ? state.time >= eventStart : state.time > eventStart) && state.time <= localTime) {
				if (changed != 0) {
					changedButtons |= changed;
					pbd->SetChanged(changed);
					pbd->SetImpulse(0);

					if (state.state & changed) {
						pbd->CallCallback(PBCB_BUTTON_DOWN, 0.0f);
					}
					if (previousButtons & changed) {
						pbd->CallCallback(PBCB_BUTTON_UP, 0.0f);
					}
				}

				if (state.impulse != 0) {
					impulse = state.impulse;
					pbd->SetChanged(0);
					pbd->SetImpulse(state.impulse);
					pbd->CallCallback(PBCB_IMPULSE, 0.0f);
				}
			}

			previousButtons = state.state;
		}

		pbd->SetButtons(currentButtons);
		pbd->SetChanged(changedButtons);
		pbd->SetImpulse(impulse);
	}

	return expired;
}

/*
=====================
rvDeclPlayback::Parse
=====================
*/
bool rvDeclPlayback::Parse(const char* text, const int textLength) {
	idLexer src;
	idToken	token;

	FreeData();

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_LEXER_FLAGS);
	src.SkipUntilString("{");

	if ( !ParseSequence(&src) ) {
		return false;
	}

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if (!token.Icmp("}")) {
			SetOrigin();
			return true;
		}
		else if (token == "data")
		{
			if ( !ParseData(&src) ) {
				return false;
			}
			continue;
		}
		else if (token == "buttons")
		{
			if ( !ParseButtons(&src) ) {
				return false;
			}
			continue;
		}
		else
		{
			src.Error("Unexpected token %s", token.c_str());
			return false;
		}
	}

	return false;
}

/*
=====================
rvDeclPlayback::FreeData
=====================
*/
void rvDeclPlayback::FreeData(void) {
	flags = 0;
	frameRate = 15.0f;
	duration = 0.0f;
	origin.Zero();
	bounds.Clear();
	points.Clear();
	angles.Clear();
	buttons.Clear();
}

/*
=====================
rvDeclPlayback::RebuildTextSource
=====================
*/
bool rvDeclPlayback::RebuildTextSource(void) {
	idFile_Memory f;

	f.WriteFloatString("\nplayback %s\n{\n", GetName());
	WriteSequence(f);
	WriteData(f);
	WriteButtons(f);
	f.WriteFloatString("}\n\n");

	SetText(f.GetDataPtr());
	return true;
}

/*
=====================
rvDeclPlayback::WriteData
=====================
*/
void rvDeclPlayback::WriteData(idFile_Memory& f) {
	const int numPoints = points.GetNumValues();
	const int numAngles = angles.GetNumValues();
	const int numSamples = numPoints > numAngles ? numPoints : numAngles;

	if (numSamples <= 0 || (flags & (PBFL_GET_POSITION | PBFL_GET_ANGLES)) == 0) {
		return;
	}

	idVec3 oldPosition;
	idAngles oldAngles;
	oldPosition.Zero();
	oldAngles.Zero();

	f.WriteFloatString("\tdata\n\t{\n");
	for (int i = 0; i < numSamples; ++i) {
		f.WriteFloatString("\t\t{ ");

		if ((flags & PBFL_GET_POSITION) && i < numPoints) {
			const idVec3 position = points.GetValue(i);
			if (idMath::Fabs(position.x - oldPosition.x) > 0.0625f ||
				idMath::Fabs(position.y - oldPosition.y) > 0.0625f ||
				idMath::Fabs(position.z - oldPosition.z) > 0.0625f) {
				f.WriteFloatString("pos %.1f,%.1f,%.1f ", position.x, position.y, position.z);
				oldPosition = position;
			}
		}

		if ((flags & PBFL_GET_ANGLES) && i < numAngles) {
			const idAngles sampleAngles = angles.GetValue(i);
			if (idMath::Fabs(sampleAngles.pitch - oldAngles.pitch) > 0.0625f ||
				idMath::Fabs(sampleAngles.yaw - oldAngles.yaw) > 0.0625f ||
				idMath::Fabs(sampleAngles.roll - oldAngles.roll) > 0.0625f) {
				if (sampleAngles.roll == 0.0f) {
					f.WriteFloatString("ang %.1f,%.1f ", sampleAngles.pitch, sampleAngles.yaw);
				} else {
					f.WriteFloatString("rotate %.1f,%.1f,%.1f ", sampleAngles.pitch, sampleAngles.yaw, sampleAngles.roll);
				}
				oldAngles = sampleAngles;
			}
		}

		f.WriteFloatString("}\n");
	}
	f.WriteFloatString("\t}\n");
}

/*
=====================
rvDeclPlayback::WriteButtons
=====================
*/
void rvDeclPlayback::WriteButtons(idFile_Memory& f) {
	if (buttons.Num() <= 0 || (flags & PBFL_GET_BUTTONS) == 0) {
		return;
	}

	byte previousState = 0;

	f.WriteFloatString("\tbuttons\n\t{\n");
	for (int i = 0; i < buttons.Num(); ++i) {
		const rvButtonState& state = buttons[i];
		const byte changedBits = previousState ^ state.state;

		if (changedBits == 0 && state.impulse == 0) {
			continue;
		}

		f.WriteFloatString("\t\t{ %.3g ", state.time);
		if (changedBits != 0) {
			if (state.state & changedBits) {
				f.WriteFloatString("down %d ", static_cast<int>(state.state & changedBits));
			}
			if (previousState & changedBits) {
				f.WriteFloatString("up %d ", static_cast<int>(previousState & changedBits));
			}
			previousState = state.state;
		}
		if (state.impulse != 0) {
			f.WriteFloatString("impulse %d ", static_cast<int>(state.impulse));
		}
		f.WriteFloatString("}\n");
	}
	f.WriteFloatString("\t}\n");
}

/*
=====================
rvDeclPlayback::WriteSequence
=====================
*/
void rvDeclPlayback::WriteSequence(idFile_Memory& f) {
	f.WriteFloatString("\tsequence\n\t{\n");
	f.WriteFloatString("\t\torigin\t\t%.1f,%.1f,%.1f\n", origin.x, origin.y, origin.z);
	f.WriteFloatString("\t\tframeRate\t%.1f\n", frameRate);
	f.WriteFloatString("\t}\n");
}
