/*
	Order of the hottest code at the start of the executable (`--section-ordering-file`, see the Nintendo 64 arm
	of ncine_extra_sources.cmake).

	The VR4300's instruction cache is 16 KB and direct-mapped: two lines of code 16 KB apart (or any multiple of
	it) share a cache line and evict each other. Where the functions of an inner loop end up relative to each
	other was left to the link order, so any change anywhere moved them, and a loop whose own code and the
	functions it calls happened to share lines refetched them on every iteration - the "code layout" swing of
	10-20% that every unrelated change caused (measured: a 128-byte shift of libdragon put the tile loop's
	rectangle helper onto the lines of UploadWindow() and cost half a millisecond a frame). Placed here, the
	functions each inner loop runs (the tile, sprite, line strip and warp loops, the shared upload and command
	writers they all call, the per-command issue path, the actor update and collision code and the audio
	mixer) keep their relative positions whatever else changes, and the order keeps the code all of the draw
	loops call off the cache lines of the tile, sprite and warp loops (the line strips share only those of
	ApplyDrawState(), which they call once per strip).

	Sections are matched by mangled name, so a renamed or re-signed function silently drops out of the order
	(it is then placed by the link order again, as everything not listed here is) - re-check this list when
	one of these functions changes.
*/
.text : {
	*(.boot)

	/* The tile layer loop */
	*(.text._ZN5nCine3RHI3RDP9RdpDevice16DispatchTileMeshE*)
	/* What every draw loop calls */
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L14ApplyDrawStateE*)
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L18EnsureTlutResidentE*)
	*(.text.__rdpq_texture_rectangle)
	*(.text.__rdpq_write8)
	*(.text.__rdpq_write8_syncchange)
	*(.text.__rdpq_write8_syncchangeuse)
	*(.text.__rdpq_fixup_write8_syncchange)
	*(.text.__rdpq_autosync_change)
	*(.text.rspq_next_buffer)
	*libc.a:libc_a-memset.o(.text)
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L12UploadWindowE*)
	/* The sprite instance loop */
	*(.text._ZN5nCine3RHI3RDP9RdpDevice8DispatchE*)
	/* The weapon wheel's line strips */
	*(.text._ZN5nCine3RHI3RDP9RdpDevice17DispatchLineStripE*)
	*(.text.rdpq_triangle_rsp)
	*(.text.rdpq_triangle)
	*(.text.__wrap_floorf)
	*libm.a:libm_a-s_floor.o(.text)
	/* Once per draw */
	*(.text._ZN5nCine3RHI3RDP9RdpDevice12PresentFrameE*)
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L19ResolveTextureStateE*)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice15ResolveProjViewE*)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice17ComputeRasterFoldE*)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice12ApplyScissorE*)
	/* The textured background's warp */
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L20SubmitStripPrimitiveE*)
	*(.text._ZNK5nCine3RHI3RDP13EffectContext16ApplyPassToStateE*)
	*(.text._ZN5nCine3RHI3RDP13EffectContext17SubmitStripShadedE*)
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L25TexturedBackground_EffectE*)
	/* Sprites cut into bands, and the lighting composite */
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L18SubmitTexturedRectE*)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice28ApplyPendingSoftwareLightingE*)
	/* The update of the scene graph, placed so that the issue path lands next to it */
	*(.text._ZN5nCine9SceneNode8OnUpdateEf*)
	*(.text._ZN5nCine9SceneNode9transformEv)
	/* The issue path of every render command */
	*(.text._ZN5nCine11RenderQueue4DrawEv)
	*(.text._ZN5nCine13RenderCommand26CommitCameraTransformationEv)
	*(.text._ZN5nCine13RenderCommand5IssueEv)
	*(.text._ZN5nCine8Material4BindEv)
	*(.text._ZN5nCine3RHI3RDP16RdpShaderProgram3UseEv)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice11BindProgramE*)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice11BindTextureE*)
	*(.text._ZN5nCine3RHI3RDP22RdpShaderUniformBlocks4BindEv)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice16BindUniformRangeE*)
	*(.text._ZN5nCine3RHI3RDP17RdpShaderUniforms14CommitUniformsEv)
	*(.text._ZN5nCine3RHI3RDP15RdpUniformCache11CommitValueEv)
	*(.text._ZN5nCine8Material18DefineVertexFormatE*)
	*(.text._ZN5nCine3RHI3RDP16RdpShaderProgram18DefineVertexFormatE*)
	*(.text._ZN5nCine8Geometry4BindEv)
	*(.text._ZN5nCine8Geometry4DrawEl)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice10DrawArraysE*)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice15ApplyDrawTargetEv)
	*(.text._ZN5nCine3RHI3RDP9RdpDevice18SetBlendingFactorsE*)
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L18Mat4MulTransform2DE*)
	*(.text._ZN5nCine3RHI3RDP12_GLOBAL__N_1L11ClampWindowE*)
	*(.text._ZN5nCine3RHI3RDP10RdpTexture14AcquireSurfaceEv)
	/* Actor updates, collisions and events */
	*(.text._ZN5Jazz26Actors9ActorBase8OnUpdateEf)
	*(.text._ZN5Jazz26Actors9ActorBase19TryStandardMovementE*)
	*(.text._ZN5Jazz26Actors9ActorBase13MoveInstantlyE*)
	*(.text._ZN5Jazz212LevelHandler15IsPositionEmptyE*)
	*(.text._ZZN5Death10Containers8FunctionIFbPN5Jazz26Actors9ActorBaseEEEC4IRZNS2_12LevelHandler15IsPositionEmpty*)
	*(.text._ZN5Jazz25Tiles7TileMap11IsTileEmptyE*)
	*(.text._ZNK5Jazz210Collisions11DynamicTree5QueryIZNS_12LevelHandler25FindCollisionActorsByAABB*)
	*(.text._ZN5Jazz26Actors9ActorBase12UpdateHitboxE*)
	*(.text._ZN5Jazz26Actors9ActorBase10UpdateAABBE*)
	*(.text._ZN5Jazz26Actors9ActorBase13ActorRenderer8OnUpdateE*)
	*(.text._ZN5Jazz26Actors9ActorBase22UpdateRendererPositionE*)
	*(.text._ZN5nCine10BaseSprite10setTexRectE*)
	*(.text._ZN5Jazz26Actors12Collectibles15CollectibleBase8OnUpdateE*)
	*(.text._ZN5Jazz212LevelHandler17ResolveCollisionsE*)
	*(.text._ZN5Jazz210Collisions11DynamicTree9MoveProxyE*)
	*(.text._ZN5Jazz210Collisions21DynamicTreeBroadPhase9MoveProxyE*)
	*(.text._ZN5Jazz210Collisions21DynamicTreeBroadPhase10BufferMoveE*)
	*(.text._ZN5Jazz210Collisions11DynamicTree10RemoveLeafE*)
	*(.text._ZN5Jazz210Collisions11DynamicTree10InsertLeafE*)
	*(.text._ZN5Jazz210Collisions11DynamicTree7BalanceE*)
	*(.text._ZNK5Jazz210Collisions11DynamicTree5QueryINS0_21DynamicTreeBroadPhaseEE*)
	*(.text._ZN5Jazz210Collisions21DynamicTreeBroadPhase16OnCollisionQueryE*)
	*(.text._ZSt16__introsort_loopIPN5Jazz210Collisions13CollisionPair*)
	*(.text._ZN5Jazz212LevelHandler13ProcessEventsE*)
	*(.text._ZN5Jazz26Events8EventMap14ActivateEventsE*)
	/* The audio mixer */
	*(.text.mixer_try_play)
	*(.text.mixer_poll_async)
	*(.text.mixer_ch_apply_freq)
	*(.text.mixer_ramp_at)
	*(.text.mixer_channel_volumes)
	*(.text.mixer_fold_volumes)
	*(.text.mixer_emit_channel)
	*(.text.mixer_wave_fits)
	*(.text.mixer_channel_window)
	*(.text.samplebuffer_prefetch)
	*(.text.samplebuffer_get)
	*(.text.tick)
	*(.text.xm_tick)
	*(.text.xm_envelopes)
	*(.text.waveform_read)
	*(.text.waveform_vadpcm_read_compressed*)
}
