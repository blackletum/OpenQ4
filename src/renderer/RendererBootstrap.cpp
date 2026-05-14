// Copyright (C) 2004 Id Software, Inc.
//

#include "tr_local.h"
#include "RendererBootstrap.h"

static rendererBootstrapState_t rg_bootstrapState;

typedef struct rendererDefaultSafetyCVar_s {
	const char *	name;
	const idCVar *	cvar;
	int				expectedInteger;
} rendererDefaultSafetyCVar_t;

static void RendererBootstrap_SetPromotionReason( rendererDefaultPromotionState_t &state, const char *reason ) {
	idStr::snPrintf( state.reason, sizeof( state.reason ), "%s", reason != NULL ? reason : "unknown" );
}

static void RendererBootstrap_AppendIssue( idStr &issues, const char *issue ) {
	if ( issues.Length() > 0 ) {
		issues.Append( "," );
	}
	issues.Append( issue != NULL ? issue : "unknown" );
}

static bool RendererBootstrap_RendererRequestIsConservative( const char *rendererRequest ) {
	return rendererRequest == NULL
		|| rendererRequest[0] == '\0'
		|| idStr::Icmp( rendererRequest, "best" ) == 0
		|| idStr::Icmp( rendererRequest, "arb2" ) == 0;
}

static bool RendererBootstrap_BuildDefaultSafetyReport( idStr &report, idStr &issues ) {
	static const rendererDefaultSafetyCVar_t guardedCVars[] = {
		{ "r_rendererModernExecutor", &r_rendererModernExecutor, 0 },
		{ "r_rendererModernSubmit", &r_rendererModernSubmit, 0 },
		{ "r_rendererModernAutoPromote", &r_rendererModernAutoPromote, 0 },
		{ "r_rendererModernVisible", &r_rendererModernVisible, 0 },
		{ "r_rendererModernVisibleDepth", &r_rendererModernVisibleDepth, 0 },
		{ "r_rendererModernDepthDebug", &r_rendererModernDepthDebug, 0 },
		{ "r_rendererModernOpaque", &r_rendererModernOpaque, 0 },
		{ "r_rendererModernGBufferDebug", &r_rendererModernGBufferDebug, 0 },
		{ "r_rendererModernDeferred", &r_rendererModernDeferred, 0 },
		{ "r_rendererModernDeferredDebug", &r_rendererModernDeferredDebug, 0 },
		{ "r_rendererForwardPlus", &r_rendererForwardPlus, 0 },
		{ "r_rendererClusterDebug", &r_rendererClusterDebug, 0 },
		{ "r_rendererGpuValidation", &r_rendererGpuValidation, 0 },
		{ "r_rendererBindless", &r_rendererBindless, 0 },
		{ "r_rendererShaderReload", &r_rendererShaderReload, 0 }
	};

	issues.Clear();
	bool conservative = true;

	if ( !RendererBootstrap_RendererRequestIsConservative( r_renderer.GetString() ) ) {
		RendererBootstrap_AppendIssue( issues, "r_renderer" );
		conservative = false;
	}
	if ( idStr::Icmp( r_glTier.GetString(), "auto" ) != 0 ) {
		RendererBootstrap_AppendIssue( issues, "r_glTier" );
		conservative = false;
	}
	if ( !rg_bootstrapState.legacyBridgeActive ) {
		RendererBootstrap_AppendIssue( issues, "rollback" );
		conservative = false;
	}

	for ( int i = 0; i < static_cast<int>( sizeof( guardedCVars ) / sizeof( guardedCVars[0] ) ); ++i ) {
		if ( guardedCVars[i].cvar->GetInteger() != guardedCVars[i].expectedInteger ) {
			RendererBootstrap_AppendIssue( issues, guardedCVars[i].name );
			conservative = false;
		}
	}

	report = va(
		"renderer=%s glTier=%s executor=%d submit=%d autoPromote=%d visible=%d visibleDepth=%d depthDebug=%d opaque=%d gbufferDebug=%d deferred=%d deferredDebug=%d forwardPlus=%d clusterDebug=%d gpuValidation=%d bindless=%d shaderReload=%d rollback=%s issues=%s",
		r_renderer.GetString(),
		r_glTier.GetString(),
		r_rendererModernExecutor.GetInteger(),
		r_rendererModernSubmit.GetInteger(),
		r_rendererModernAutoPromote.GetInteger(),
		r_rendererModernVisible.GetInteger(),
		r_rendererModernVisibleDepth.GetInteger(),
		r_rendererModernDepthDebug.GetInteger(),
		r_rendererModernOpaque.GetInteger(),
		r_rendererModernGBufferDebug.GetInteger(),
		r_rendererModernDeferred.GetInteger(),
		r_rendererModernDeferredDebug.GetInteger(),
		r_rendererForwardPlus.GetInteger(),
		r_rendererClusterDebug.GetInteger(),
		r_rendererGpuValidation.GetInteger(),
		r_rendererBindless.GetInteger(),
		r_rendererShaderReload.GetInteger(),
		rg_bootstrapState.legacyBridgeActive ? "available" : "missing",
		issues.Length() > 0 ? issues.c_str() : "none" );

	return conservative;
}

static const char *RendererBootstrap_PreferenceName( rendererTierPreference_t preference ) {
	switch ( preference ) {
	case RENDERER_TIER_PREF_LEGACY:
		return "legacy";
	case RENDERER_TIER_PREF_GL33:
		return "gl33";
	case RENDERER_TIER_PREF_GL41:
		return "gl41";
	case RENDERER_TIER_PREF_GL43:
		return "gl43";
	case RENDERER_TIER_PREF_GL45:
		return "gl45";
	case RENDERER_TIER_PREF_GL46:
		return "gl46";
	case RENDERER_TIER_PREF_AUTO:
	default:
		return "auto";
	}
}

static bool RendererBootstrap_RendererRequestAllowsPromotion( const char *rendererRequest ) {
	return rendererRequest == NULL || rendererRequest[0] == '\0' || idStr::Icmp( rendererRequest, "best" ) == 0;
}

static rendererDefaultPromotionState_t RendererBootstrap_EvaluateDefaultPromotion(
	rendererTierPreference_t requestedPreference,
	rendererTier_t selectedTier,
	const renderFeatureSet_t &features,
	bool legacyBridgeActive,
	bool modernExecutorAvailable,
	bool manualSignoffEnabled,
	const char *rendererRequest,
	const rendererDriverQuirkReport_t &quirkReport ) {
	rendererDefaultPromotionState_t state;
	memset( &state, 0, sizeof( state ) );

	const unsigned int blockingQuirks =
		RENDERER_DRIVER_QUIRK_FORCE_LEGACY |
		RENDERER_DRIVER_QUIRK_DISABLE_UBO |
		RENDERER_DRIVER_QUIRK_DISABLE_MRT;

	state.requestedAutoTier = requestedPreference == RENDERER_TIER_PREF_AUTO;
	state.rendererRequestAllowsPromotion = RendererBootstrap_RendererRequestAllowsPromotion( rendererRequest );
	state.selectedModernTier = RendererTier_IsModern( selectedTier );
	state.compatibilityGatesPassed =
		state.selectedModernTier &&
		features.modernBaseline &&
		features.shaderLibrary &&
		features.scenePackets &&
		features.renderGraph &&
		( quirkReport.flags & blockingQuirks ) == 0;
	state.modernExecutorAvailable = modernExecutorAvailable;
	state.legacyEscapeAvailable = legacyBridgeActive;
	state.manualSignoffEnabled = manualSignoffEnabled;

	state.eligible =
		state.requestedAutoTier &&
		state.rendererRequestAllowsPromotion &&
		state.selectedModernTier &&
		state.compatibilityGatesPassed &&
		state.modernExecutorAvailable &&
		state.legacyEscapeAvailable;
	state.active = state.eligible && state.manualSignoffEnabled;

	if ( !state.requestedAutoTier ) {
		RendererBootstrap_SetPromotionReason( state, "r-gl-tier-not-auto" );
	} else if ( !state.rendererRequestAllowsPromotion ) {
		RendererBootstrap_SetPromotionReason( state, "explicit-renderer-escape" );
	} else if ( !state.selectedModernTier ) {
		RendererBootstrap_SetPromotionReason( state, "selected-tier-not-modern" );
	} else if ( !state.compatibilityGatesPassed ) {
		RendererBootstrap_SetPromotionReason( state, "compatibility-gates-blocked" );
	} else if ( !state.modernExecutorAvailable ) {
		RendererBootstrap_SetPromotionReason( state, "modern-executor-unavailable" );
	} else if ( !state.legacyEscapeAvailable ) {
		RendererBootstrap_SetPromotionReason( state, "legacy-escape-unavailable" );
	} else if ( !state.manualSignoffEnabled ) {
		RendererBootstrap_SetPromotionReason( state, "manual-signoff-required" );
	} else {
		RendererBootstrap_SetPromotionReason( state, "auto-promoted" );
	}

	return state;
}

static void RendererBootstrap_UpdateDefaultPromotionState( void ) {
	rg_bootstrapState.defaultPromotion = RendererBootstrap_EvaluateDefaultPromotion(
		rg_bootstrapState.requestedPreference,
		rg_bootstrapState.selectedTier,
		rg_bootstrapState.features,
		rg_bootstrapState.legacyBridgeActive,
		rg_bootstrapState.modernExecutorAvailable,
		r_rendererModernAutoPromote.GetBool(),
		r_renderer.GetString(),
		RendererDriverQuirks_LastReport() );
}

static void RendererBootstrap_UpdateSummary( const renderBackendCaps_t &caps ) {
	RendererBootstrap_UpdateDefaultPromotionState();

	char capsSummary[384];
	RendererCaps_FormatSummary( caps, capsSummary, sizeof( capsSummary ) );
	idStr::snPrintf(
		rg_bootstrapState.summary,
		sizeof( rg_bootstrapState.summary ),
		"selected=%s requested=%s bridge=%s modernExecutor=%s defaultVisible=%s reason=%s caps={%s}",
		RendererTier_Name( rg_bootstrapState.selectedTier ),
		RendererBootstrap_PreferenceName( rg_bootstrapState.requestedPreference ),
		rg_bootstrapState.legacyBridgeActive ? "ARB2" : "none",
		rg_bootstrapState.modernExecutorAvailable ? "yes" : "no",
		rg_bootstrapState.defaultPromotion.active ? "modern" : "ARB2",
		rg_bootstrapState.defaultPromotion.reason,
		capsSummary );
}

void RendererBootstrap_BeginOpenGL( const renderBackendCaps_t &caps, const char *tierPreference ) {
	memset( &rg_bootstrapState, 0, sizeof( rg_bootstrapState ) );
	rg_bootstrapState.requestedPreference = RendererTierPreference_FromString( tierPreference );
	rg_bootstrapState.selectedTier = RendererTier_Select( caps, rg_bootstrapState.requestedPreference );
	rg_bootstrapState.features = RendererFeatureSet_Build( caps, rg_bootstrapState.selectedTier );
	rg_bootstrapState.modernExecutorAvailable = false;
	RendererBootstrap_UpdateSummary( caps );
}

void RendererBootstrap_FinalizeLegacyBridge( bool allowARB2Path ) {
	rg_bootstrapState.legacyBridgeActive = allowARB2Path;
	rg_bootstrapState.features.legacyARB2Bridge = allowARB2Path;
	RendererBootstrap_UpdateSummary( glConfig.backendCaps );
	common->Printf( "Renderer bootstrap: %s\n", rg_bootstrapState.summary );
	if ( RendererTier_IsModern( rg_bootstrapState.selectedTier ) && !rg_bootstrapState.modernExecutorAvailable ) {
		common->Printf( "Renderer bootstrap: modern tier is available, but execution is currently routed through the ARB2 compatibility bridge\n" );
	}
}

void RendererBootstrap_SetModernExecutorAvailable( bool available ) {
	rg_bootstrapState.modernExecutorAvailable = available;
	RendererBootstrap_UpdateSummary( glConfig.backendCaps );
}

bool RendererBootstrap_ShouldAutoPromoteModernVisible( void ) {
	RendererBootstrap_UpdateDefaultPromotionState();
	return rg_bootstrapState.defaultPromotion.active;
}

void RendererBootstrap_PrintGfxInfo( void ) {
	RendererBootstrap_UpdateDefaultPromotionState();
	const rendererDefaultPromotionState_t &promotion = rg_bootstrapState.defaultPromotion;
	common->Printf(
		"Renderer default promotion: cvar=%d manualVisible=%d active=%d eligible=%d autoTier=%d rendererAllows=%d modernTier=%d gates=%d executor=%d legacyEscape=%d reason=%s\n",
		r_rendererModernAutoPromote.GetBool() ? 1 : 0,
		r_rendererModernVisible.GetBool() ? 1 : 0,
		promotion.active ? 1 : 0,
		promotion.eligible ? 1 : 0,
		promotion.requestedAutoTier ? 1 : 0,
		promotion.rendererRequestAllowsPromotion ? 1 : 0,
		promotion.selectedModernTier ? 1 : 0,
		promotion.compatibilityGatesPassed ? 1 : 0,
		promotion.modernExecutorAvailable ? 1 : 0,
		promotion.legacyEscapeAvailable ? 1 : 0,
		promotion.reason );

	idStr safetyReport;
	idStr safetyIssues;
	const bool conservativeDefaults = RendererBootstrap_BuildDefaultSafetyReport( safetyReport, safetyIssues );
	common->Printf(
		"Renderer default safety: conservative=%d %s\n",
		conservativeDefaults ? 1 : 0,
		safetyReport.c_str() );
}

void RendererBootstrap_Shutdown( void ) {
	memset( &rg_bootstrapState, 0, sizeof( rg_bootstrapState ) );
}

const rendererBootstrapState_t &RendererBootstrap_GetState( void ) {
	return rg_bootstrapState;
}

bool RendererDefaultSafety_RunSelfTest( void ) {
	idStr report;
	idStr issues;
	const bool conservativeDefaults = RendererBootstrap_BuildDefaultSafetyReport( report, issues );

	common->Printf(
		"Renderer default safety: conservative=%d %s\n",
		conservativeDefaults ? 1 : 0,
		report.c_str() );

	if ( !conservativeDefaults ) {
		common->Printf(
			"RendererDefaultSafety self-test failed: non-conservative defaults (%s)\n",
			issues.Length() > 0 ? issues.c_str() : "unknown" );
		return false;
	}

	common->Printf( "RendererDefaultSafety self-test passed\n" );
	return true;
}

bool RendererDefaultPromotion_RunSelfTest( void ) {
	rendererDriverQuirkReport_t cleanReport;
	memset( &cleanReport, 0, sizeof( cleanReport ) );

	renderFeatureSet_t modernFeatures;
	memset( &modernFeatures, 0, sizeof( modernFeatures ) );
	modernFeatures.modernBaseline = true;
	modernFeatures.shaderLibrary = true;
	modernFeatures.scenePackets = true;
	modernFeatures.renderGraph = true;

	bool ok = true;
	int cases = 0;

	rendererDefaultPromotionState_t state = RendererBootstrap_EvaluateDefaultPromotion(
		RENDERER_TIER_PREF_AUTO,
		RENDERER_TIER_TOP_GL46,
		modernFeatures,
		true,
		true,
		false,
		"best",
		cleanReport );
	if ( !state.eligible || state.active || idStr::Icmp( state.reason, "manual-signoff-required" ) != 0 ) {
		common->Printf(
			"RendererDefaultPromotion self-test failed: unsigned eligible path mismatch (eligible=%d active=%d reason=%s)\n",
			state.eligible ? 1 : 0,
			state.active ? 1 : 0,
			state.reason );
		ok = false;
	}
	cases++;

	state = RendererBootstrap_EvaluateDefaultPromotion(
		RENDERER_TIER_PREF_AUTO,
		RENDERER_TIER_TOP_GL46,
		modernFeatures,
		true,
		true,
		true,
		"best",
		cleanReport );
	if ( !state.eligible || !state.active || idStr::Icmp( state.reason, "auto-promoted" ) != 0 ) {
		common->Printf(
			"RendererDefaultPromotion self-test failed: signed auto path mismatch (eligible=%d active=%d reason=%s)\n",
			state.eligible ? 1 : 0,
			state.active ? 1 : 0,
			state.reason );
		ok = false;
	}
	cases++;

	state = RendererBootstrap_EvaluateDefaultPromotion(
		RENDERER_TIER_PREF_LEGACY,
		RENDERER_TIER_LEGACY_GL2_COMPAT,
		modernFeatures,
		true,
		true,
		true,
		"best",
		cleanReport );
	if ( state.eligible || state.active || idStr::Icmp( state.reason, "r-gl-tier-not-auto" ) != 0 ) {
		common->Printf(
			"RendererDefaultPromotion self-test failed: legacy tier escape mismatch (eligible=%d active=%d reason=%s)\n",
			state.eligible ? 1 : 0,
			state.active ? 1 : 0,
			state.reason );
		ok = false;
	}
	cases++;

	state = RendererBootstrap_EvaluateDefaultPromotion(
		RENDERER_TIER_PREF_AUTO,
		RENDERER_TIER_TOP_GL46,
		modernFeatures,
		true,
		true,
		true,
		"arb2",
		cleanReport );
	if ( state.eligible || state.active || idStr::Icmp( state.reason, "explicit-renderer-escape" ) != 0 ) {
		common->Printf(
			"RendererDefaultPromotion self-test failed: explicit ARB2 escape mismatch (eligible=%d active=%d reason=%s)\n",
			state.eligible ? 1 : 0,
			state.active ? 1 : 0,
			state.reason );
		ok = false;
	}
	cases++;

	rendererDriverQuirkReport_t blockedReport = cleanReport;
	blockedReport.flags = RENDERER_DRIVER_QUIRK_DISABLE_UBO;
	state = RendererBootstrap_EvaluateDefaultPromotion(
		RENDERER_TIER_PREF_AUTO,
		RENDERER_TIER_TOP_GL46,
		modernFeatures,
		true,
		true,
		true,
		"best",
		blockedReport );
	if ( state.eligible || state.active || idStr::Icmp( state.reason, "compatibility-gates-blocked" ) != 0 ) {
		common->Printf(
			"RendererDefaultPromotion self-test failed: compatibility gate mismatch (eligible=%d active=%d reason=%s)\n",
			state.eligible ? 1 : 0,
			state.active ? 1 : 0,
			state.reason );
		ok = false;
	}
	cases++;

	state = RendererBootstrap_EvaluateDefaultPromotion(
		RENDERER_TIER_PREF_AUTO,
		RENDERER_TIER_TOP_GL46,
		modernFeatures,
		false,
		true,
		true,
		"best",
		cleanReport );
	if ( state.eligible || state.active || idStr::Icmp( state.reason, "legacy-escape-unavailable" ) != 0 ) {
		common->Printf(
			"RendererDefaultPromotion self-test failed: missing legacy escape mismatch (eligible=%d active=%d reason=%s)\n",
			state.eligible ? 1 : 0,
			state.active ? 1 : 0,
			state.reason );
		ok = false;
	}
	cases++;

	if ( !ok ) {
		return false;
	}

	common->Printf( "RendererDefaultPromotion self-test passed (cases=%d)\n", cases );
	return true;
}
