#pragma once

// Experimental, off-by-default TF2 rendering-only clock A/B test.
bool MirvFix_SetDemoRenderClock(bool enabled);
bool MirvFix_GetDemoRenderClock();
void MirvFix_ResetDemoRenderClock();
void MirvFix_DemoRenderClockBegin();
void MirvFix_DemoRenderClockEnd();
void MirvFix_PrintDemoRenderClockStatus();
