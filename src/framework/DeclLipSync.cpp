// DeclLipSync.cpp
//



/*
===================
rvDeclPlayerModel::SetLipSyncData
===================
*/
void rvDeclLipSync::SetLipSyncData(const char* lsd, const char* lang) {
	if ( lsd == NULL ) {
		lsd = "";
	}
	if ( lang == NULL ) {
		lang = "";
	}

	if ( !strchr( lsd, '%' ) ) {
		mLipSyncData.Set(lang, lsd);
		return;
	}

	common->Warning("SetLipSyncData: language %s for lipsync '%s' has invalid character %% in it", lang, GetName());
}

/*
===================
rvDeclPlayerModel::Size
===================
*/
size_t rvDeclLipSync::Size(void) const {
	return sizeof(rvDeclLipSync)
		+ mDescription.Allocated()
		+ mTranscribeText.Allocated()
		+ mHMM.Allocated()
		+ mLipSyncData.Allocated();
}

/*
===================
rvDeclPlayerModel::DefaultDefinition
===================
*/
const char* rvDeclLipSync::DefaultDefinition() const {
	return "{ description \"<DEFAULTED>\" }";
}

/*
===================
rvDeclPlayerModel::FreeData
===================
*/
bool rvDeclLipSync::Parse(const char* text, const int textLength) {
	idLexer src;
	idToken	token, token2;

	FreeData();
	mHMM = "male";

	src.LoadMemory(text, textLength, GetFileName(), GetLineNum());
	src.SetFlags(DECL_LEXER_FLAGS);
	src.SkipUntilString("{");

	while (1) {
		if (!src.ReadToken(&token)) {
			break;
		}

		if (!token.Icmp("}")) {
			break;
		}

		if (token == "visemes")
		{
			idToken lang;
			idToken data;

			src.ReadToken(&lang);
			src.ReadToken(&data);

			SetLipSyncData(data.c_str(), lang.c_str());
			continue;
		}
		else if (token == "hmm")
		{
			src.ReadToken(&token);
			mHMM = token;
			continue;
		}
		else if (token == "text")
		{
			src.ReadToken(&token);
			mTranscribeText = token;
			continue;
		}
		else if (token == "description")
		{
			src.ReadToken(&token);
			mDescription = token;
			continue;
		}
		else
		{
			src.Error("Invalid or unexpected token %s\n", token.c_str());
			return false;
		}
	}
	return true;
}

/*
===================
rvDeclLipSync::FreeData
===================
*/
void rvDeclLipSync::FreeData(void) {
	mDescription.Clear();
	mTranscribeText.Clear();
	mHMM.Clear();
	mLipSyncData.Clear();
}
