#include "EditorIcons.h"

#if defined(WITH_EDITOR)

namespace Jazz2::Editor
{
	namespace
	{
		// Vector art of the editor's own icons on a square of 16 units, see UI::Windows::VectorArt
		const char* GetVectorArt(EditorIcon icon)
		{
			switch (icon) {
				case EditorIcon::Brush:
					return "stroke # 2.4 round: M13.5 2.5 L8.2 7.8; "
						"fill #: M7.8 8.6 L9.4 10.2 Q8.6 13.6 5.2 14.2 Q3 14.6 1.2 14.8 Q2.4 13.6 2.6 11.6 Q3 8.8 7.8 8.6 Z";
				case EditorIcon::Eraser:
					return "fill #: M9 2.2 L14 7 L9.4 11.6 L4.2 6.8 Z; stroke # 1 round-join: M9 2.2 L14 7 L9.4 11.6 L4.2 6.8 Z; "
						"stroke # 1.5 0.65 round: M4.2 7 L1.5 9.7 V10.6 L3.4 12.5 H8.6 L9.4 11.6; stroke # 1.2: M2 14.5 H13";
				case EditorIcon::Rectangle: return "stroke # 1: M2.5 2.5 H13.5 V12.5 H2.5 Z; stroke + 1: M3.5 3.5 H12.5 V11.5 H3.5 Z";
				case EditorIcon::Fill:
					return "stroke # 1.2 round-join: M6.5 2 L1.8 7.2 H11.2 Z; fill #: M1.8 7.2 H11.2 L6.5 12 Z; "
						"stroke # 1 round-join: M1.8 7.2 H11.2 L6.5 12 Z; "
						"fill #: M12.8 7.6 C13.6 8.6 14.6 9.6 14.6 10.6 A1.5 1.5 0 0 1 11.6 10.6 C11.6 9.6 12.2 8.6 12.8 7.6 Z; "
						"stroke # 1: M2 14.5 H14";
				case EditorIcon::Eyedropper:
					return "stroke # 3.6 round: M10.9 5.1 L12.7 3.3; stroke # 1.6 round: M8 4.6 L11.4 8; stroke # 2.2 0.4 round: "
						"M8.8 7.2 L3.8 12.2; stroke # 2.2 round: M6 10 L3.8 12.2; stroke # 1.1 round: M3.2 12.8 L1.8 14.2";
				case EditorIcon::Select: return "stroke # 1: M2.5 4 V2.5 H4.5 M5.5 2.5 H7.5 M8.5 2.5 H10.5 M11.5 2.5 H13.5 V4 M13.5 5 V7 M13.5 8 V10 M13.5 11 V12.5 H11.5 M10.5 12.5 H8.5 M7.5 12.5 H5.5 M4.5 12.5 H2.5 V11 M2.5 10 V8 M2.5 7 V5";
				case EditorIcon::Wand:
					return "stroke # 1.5 round: M8.2 7.8 L1.8 14.2; "
						"fill #: M10.5 1.2 Q11.1 4.9 14.8 5.5 Q11.1 6.1 10.5 9.8 Q9.9 6.1 6.2 5.5 Q9.9 4.9 10.5 1.2 Z; "
						"stroke + 1 round: M6.8 2.3 L7.6 3.1 M14.2 2.3 L13.4 3.1 M13.4 7.9 L14.2 8.7";
				case EditorIcon::Hand:
					return "fill # 0.4: M4.5 9 V3 Q4.5 1.5 6 1.5 Q7.5 1.5 7.5 3 V6.9 Q8 6.4 8.9 6.4 Q9.9 6.4 10.1 7.3 Q10.5 6.8 "
						"11.4 6.8 Q12.4 6.8 12.6 7.8 Q13 7.4 13.6 7.5 Q14.5 7.7 14.5 8.6 V11.3 Q14.5 14.5 11.5 14.5 H7.4 Q5.6 "
						"14.5 4.6 13 L2.1 9.7 Q1.6 8.9 2.3 8.5 Q3 8.1 3.7 8.8 Z; fill # 0.75: M5.3 8.5 V3 Q5.3 2.3 6 2.3 Q6.7 "
						"2.3 6.7 3 V8.5 Z; stroke # 0.9 round: M4.5 9 V3 Q4.5 1.5 6 1.5 Q7.5 1.5 7.5 3 V6.9 Q8 6.4 8.9 6.4 "
						"Q9.9 6.4 10.1 7.3 Q10.5 6.8 11.4 6.8 Q12.4 6.8 12.6 7.8 Q13 7.4 13.6 7.5 Q14.5 7.7 14.5 8.6 V11.3 "
						"Q14.5 14.5 11.5 14.5 H7.4 Q5.6 14.5 4.6 13 L2.1 9.7 Q1.6 8.9 2.3 8.5 Q3 8.1 3.7 8.8 L4.5 9.8; stroke "
						"# 0.7 0.8 round: M7.5 6.9 V9.4 M10.1 7.3 V9.6 M12.6 7.8 V9.8";
				case EditorIcon::Undo: return "fill #: M1.5 5 L6 1 V9 Z; stroke # 2 round: M5.5 5 H10 C14.5 5 14.5 12 10 12 H5";
				case EditorIcon::Redo: return "fill #: M14.5 5 L10 1 V9 Z; stroke # 2 round: M10.5 5 H6 C1.5 5 1.5 12 6 12 H11";
				case EditorIcon::Grid: return "stroke # 1: rect 1.5 1.5 13 13 1.5; stroke # 1: M5.5 2 V14 M10.5 2 V14 M2 5.5 H14 M2 10.5 H14";
				case EditorIcon::Eye: return "stroke # 1.3 round: M1 8 Q8 0.5 15 8 Q8 15.5 1 8 Z; fill #: circle 8 8 2.7";
				case EditorIcon::EyeClosed: return "stroke # 1.3 round: M1 7.5 Q8 14.5 15 7.5 M3.8 10.1 L2.6 12.4 M6.5 11 V13 M9.5 11 V13 M12.2 10.1 L13.4 12.4";
				case EditorIcon::Lock:
					return "stroke # 1.5: M5.25 7.5 V5.5 A2.75 2.75 0 0 1 10.75 5.5 V7.5; fill #: rect 3 7.5 10 6.5 1; "
						"clear: circle 8 10.2 1.2; clear: rect 7.4 10.4 1.2 2";
				case EditorIcon::Unlock:
					return "stroke # 1.5: M6.25 5 V3.5 A2.75 2.75 0 0 1 11.75 3.5 V7.5; fill #: rect 3 7.5 10 6.5 1; "
						"clear: circle 8 10.2 1.2; clear: rect 7.4 10.4 1.2 2";
				case EditorIcon::Layers:
					return "fill #: M8 1.3 L14.8 4.5 L8 7.7 L1.2 4.5 Z; stroke # 0.6 round: M8 1.3 L14.8 4.5 L8 7.7 L1.2 4.5 Z; "
						"stroke # 1.4 round: M1.6 7.4 L8 10.5 L14.4 7.4 M1.6 10.4 L8 13.5 L14.4 10.4";
				case EditorIcon::Save:
					return "stroke # 1 round-join: M3.5 2.5 H11.2 L13.5 4.8 V12.5 Q13.5 13.5 12.5 13.5 H3.5 Q2.5 13.5 2.5 12.5 "
						"V3.5 Q2.5 2.5 3.5 2.5 Z; fill #: rect 5 2.5 5.5 3.5 0.6; clear: rect 8.2 3.2 1.3 2 0.3; fill # 0.3: "
						"rect 4.5 8.5 7 5; stroke # 1 round-join: M4.5 13.5 V9.3 Q4.5 8.5 5.3 8.5 H10.7 Q11.5 8.5 11.5 9.3 "
						"V13.5; stroke # 0.8 0.75 round: M6.2 10.6 H9.8 M6.2 12.2 H9.8";
				case EditorIcon::New:
					return "stroke # 1: M8 14.5 H3.5 V1.5 H9.5 L12.5 4.5 V8; stroke # 1 round-join: M9.5 1.5 V4.5 H12.5; "
						"stroke # 2: M12 9 V15 M9 12 H15";
				case EditorIcon::Play:
					return "outline 1; fill n: M3 2.6 Q3 1.4 4.1 2 L14.4 7.4 Q15.4 8 14.4 8.6 L4.1 14 Q3 14.6 3 13.4 Z; "
						"stroke N 1.2 round: M5 13.2 L13.6 8.6; stroke m 1.2 round: M4.2 3.6 V9.5; fill w: circle 5 4.6 0.7";
				case EditorIcon::Stop: return "fill #: rect 3 3 10 9 0.6";
				case EditorIcon::ZoomIn:
					return "stroke # 1.5: circle 7 6.5 5; stroke # 2 round: M11.2 10.7 L14.5 14; "
						"stroke # 1.5 round: M7 4.3 V8.7 M4.8 6.5 H9.2";
				case EditorIcon::ZoomOut: return "stroke # 1.5: circle 7 6.5 5; stroke # 2 round: M11.2 10.7 L14.5 14; stroke # 1.5 round: M4.8 6.5 H9.2";
				case EditorIcon::Fit:
					return "stroke # 1: M2.5 5.5 V2.5 H5.5 M10.5 2.5 H13.5 V5.5 M13.5 10.5 V13.5 H10.5 M5.5 13.5 H2.5 V10.5; "
						"stroke # 1.1 round: M4 4 L5.6 5.6 M12 4 L10.4 5.6 M12 12 L10.4 10.4 M4 12 L5.6 10.4; "
						"stroke # 1: rect 6 6 4 4 0.8";
				case EditorIcon::FlipX:
					return "fill #: M6 2.5 V13.5 H1 Z; stroke # 1 round-join: M10.5 3 V13 H14.6 Z; "
						"stroke # 2 0.5: M8 1 V3 M8 5 V7 M8 9 V11 M8 13 V15";
				case EditorIcon::FlipY:
					return "fill #: M14.5 1 V6 H2 Z; stroke # 1 round-join: M2.5 10.5 H13.5 V14.6 Z; "
						"stroke # 2 0.5: M1 8 H3 M5 8 H7 M9 8 H11 M13 8 H15";
				case EditorIcon::Copy: return "stroke # 1: rect 6.5 2.5 7 8 0.8; clear: rect 1 5 10 10; stroke # 1: rect 2.5 6.5 7 7 0.8";
				case EditorIcon::Paste:
					return "stroke # 1 round: M5.5 13.5 H3.5 Q2.5 13.5 2.5 12.5 V3.5 Q2.5 2.5 3.5 2.5 H4.5 M9.5 2.5 H10.5 Q11.5 2.5 11.5 3.5 V4.5; "
						"fill #: rect 4 1 6 3 0.8; clear: rect 6 2 2 1 0.3; fill # 0.375: rect 7 6 7 9 0.6; "
						"stroke # 1: rect 7.5 6.5 6 8 0.6; stroke # 1: M9 9.5 H12 M9 11.5 H12";
				case EditorIcon::Cut: return "stroke # 1.5 round: M4 1.5 L9.6 9.6 M12 1.5 L6.4 9.6; stroke # 1.5: circle 4.6 12.3 1.9 circle 11.4 12.3 1.9";
				case EditorIcon::Settings:
					return "fill #: circle 8 8 5.3 rect 6 1 4 4 rect 6 11 4 4 rect 1 6 4 4 rect 11 6 4 4 M3.829 6.515 L1.707 4.393 L4.393 1.707 L6.515 3.829 Z M9.485 3.829 L11.607 1.707 L14.293 4.393 L12.171 6.515 Z M12.171 9.485 L14.293 11.607 L11.607 14.293 L9.485 12.171 Z M6.515 12.171 L4.393 14.293 L1.707 11.607 L3.829 9.485 Z; "
						"clear: circle 8 8 2.4";
				case EditorIcon::Parallax:
					return "fill # 0.25: M8 3.5 L14.5 6.5 L8 9.5 L1.5 6.5 Z; "
						"stroke # 1.25 round: M8 3.5 L14.5 6.5 L8 9.5 L1.5 6.5 Z M1.5 6.5 V9.5 L8 12.5 L14.5 9.5 V6.5";
				case EditorIcon::Tiles:
					return "fill # 0.25: rect 2 2 5 5 1 rect 9 2 5 5 1 rect 2 9 5 5 1 rect 9 9 5 5 1; "
						"stroke # 1: rect 2.5 2.5 4 4 0.8 rect 9.5 2.5 4 4 0.8 rect 2.5 9.5 4 4 0.8 rect 9.5 9.5 4 4 0.8";
				case EditorIcon::Export: return "fill #: M8 1.8 L12.4 6.2 H3.6 Z; stroke # 2: M8 5 V11.2; stroke # 1.5 round: M3.75 9.5 V12.75 H12.25 V9.5";
				case EditorIcon::Event: return "fill #: rect 3 1 2 12.5 M3.2 13 H4.8 L6.8 15 H1.2 Z M4.5 2 H11 Q14.9 2.4 14.9 4.6 Q14.8 7 12 7.7 Q10.3 8.9 8.4 8.6 Q6.6 8.3 4.5 7.8 Z";
				case EditorIcon::Mask: return "stroke + 1: rect 1.5 2.5 13 11; fill #: M2 10.5 L14 4.5 V13 H2 Z";
				case EditorIcon::Pause: return "fill #: rect 4 3 3 10 0.6 rect 9 3 3 10 0.6";
				case EditorIcon::Path:
					return "fill #: circle 12.5 4 2.4 circle 3.5 13.4 1.5; "
						"stroke # 1.3 round: M12.4 6 Q12.2 7.7 9.2 8.2 Q7 8.6 6 10 L3.6 13";
				case EditorIcon::Measure:
					return "stroke # 1: rect 0.5 5.5 15 6 0.4; "
						"stroke # 1: M2.5 7 V10 M6.5 7 V10 M10.5 7 V10 M4.5 7 V8.5 M8.5 7 V8.5 M12.5 7 V8.5";
				case EditorIcon::Reach: return "stroke # 1.25 round: M4.4 7.2 Q8.5 -0.8 12.4 6; fill #: rect 10 8 5 2 rect 1 12 6 2; stroke # 1.2: M3.6 9 V11";
				case EditorIcon::SmartBrush:
					return "fill +: M0.5 5.5 H10.5 V7 L14.5 11 V14.5 H0.5 Z; stroke # 1: M0.5 5.5 H10.5 V7 L14.5 11 V14.5 H0.5 Z; "
						"fill #: rect 1 3.8 1 1.5 rect 3 3.8 1 1.5 rect 5 3.8 1 1.5 rect 7 3.8 1 1.5 rect 9 3.8 1 1.5; "
						"stroke # 1: M11 1.5 H14 M12.5 0 V3";
				case EditorIcon::Slope:
					return "fill #: M0 15 V12.6 Q2.5 10.2 6 8.2 H9 L15.6 14.8 V15 Z; stroke # 1.6 0.9 round: M3 8.5 L12.5 3; "
						"fill #: rect 1.8 7.3 2.4 2.4 0.4 rect 11.3 1.8 2.4 2.4 0.4";
				case EditorIcon::Usage: return "fill #: rect 3 8 2 4 rect 7 5 2 7 rect 11 2 2 10 rect 2 12 12 1";
				case EditorIcon::EventPath:
					return "fill #: circle 3.5 10.5 1.9 circle 8.5 6.5 1.9 circle 13.5 2.5 1.9; "
						"fill # 0.6: circle 11 4.5 0.75 circle 6 8.5 0.75";
				case EditorIcon::Map: return "stroke # 1: rect 1.5 2.5 13 9 rect 4.5 4.5 5 3; fill +: M2 11 V10.2 Q3.8 8.6 6 10 H10 Q12.2 8.6 14 10.2 V11 Z";
				case EditorIcon::RotateCW: return "fill #: M9.5 7 H15.5 L12.5 11 Z; stroke # 2 round: M7.5 12.5 A5 5 0 1 1 12.5 7.5 L12.5 8";
				case EditorIcon::RotateCCW: return "fill #: M6.5 7 H0.5 L3.5 11 Z; stroke # 2 round: M8.5 12.5 A5 5 0 1 0 3.5 7.5 L3.5 8";
				case EditorIcon::Dice:
					return "stroke # 1: rect 1.5 1.5 13 13 2; "
						"fill #: circle 4 4 1.3 circle 12 4 1.3 circle 8 8 1.3 circle 4 12 1.3 circle 12 12 1.3";
				case EditorIcon::Shuffle:
					return "stroke # 1.8: M0.2 4.8 H2.5 C6.5 4.8 6.5 12.2 10.5 12.2 H12.5 M0.2 12.2 H2.5 C6.5 12.2 6.5 4.8 10.5 4.8 H12.5; "
						"fill #: M11.6 2 L15.8 4.8 L11.6 7.6 Z M11.6 9.4 L15.8 12.2 L11.6 15 Z";
				case EditorIcon::RandomFlipX:
					return "stroke # 1: rect 5.5 1.5 5 5; fill #: circle 8 4 1; stroke # 2: M2 11 H14; "
						"fill #: M0.2 11 L3.2 7.8 V14.2 Z M15.8 11 L12.8 7.8 V14.2 Z";
				case EditorIcon::RandomFlipY:
					return "stroke # 1: rect 1.5 5.5 5 5; fill #: circle 4 8 1; stroke # 2: M12 2 V14; "
						"fill #: M12 0.6 L15.2 3.8 H8.8 Z M12 15.4 L15.2 12.2 H8.8 Z";
				case EditorIcon::ShapeLine: return "stroke # 2.2 round: M3 14 L14 3";
				case EditorIcon::Ellipse: return "stroke # 1.2: ellipse 8 8 6.4 4.4";
				case EditorIcon::FilledEllipse: return "fill #: ellipse 8 8 7 5";
				case EditorIcon::More: return "fill #: circle 3 7.5 1.5 circle 8 7.5 1.5 circle 13 7.5 1.5";
				case EditorIcon::Debug:
					return "outline 1; stroke R 2.8 round: M10 10 L13.4 13.4; stroke r 1.4 round: M9.8 9.8 L12.6 12.6; "
						"fill G: circle 6.2 6.2 5; fill g: circle 5.9 5.9 4.6; stroke w 0.9 round: M2.1 7.2 A4.2 4.2 0 0 1 6.7 2; "
						"fill B: circle 6.2 6.4 3.3; fill b: circle 5.8 6 2.9; fill w: circle 5 5.1 0.9";
				case EditorIcon::EnterEditor:
					return "outline 1; stroke p 2.8 round-cap: M10.992 5.008 L12.053 3.947; "
						"stroke P 1.4 round-cap: M11.487 5.503 L12.548 4.442; fill w: circle 11.7 3.45 0.45; "
						"stroke g 2.8: M9.578 6.422 L10.992 5.008; stroke G 1.4: M10.073 6.917 L11.487 5.503; "
						"stroke y 2.8: M5.194 10.806 L9.578 6.422; stroke Y 1.4: M5.689 11.301 L10.073 6.917; "
						"stroke w 0.6 round: M6.785 7.871 L8.906 5.75; fill t: M6.184 11.796 L4.204 9.816 L1.8 14.2 Z; "
						"fill T: M6.184 11.796 L5.194 10.806 L1.8 14.2 Z; fill G: M4.174 12.898 L3.102 11.826 L1.8 14.2 Z";
				case EditorIcon::Shapes: return "stroke # 1.2: circle 11 11 4.3; clear: rect 1 1 8 8; fill +: rect 1.5 1.5 7 7; stroke # 1: rect 1.5 1.5 7 7";
				case EditorIcon::History:
					return "stroke # 1.3 round: M4.46 12.21 A5.5 5.5 0 1 0 3.24 5.25; fill #: M0.6 4.9 H5.8 L3.2 8.5 Z; "
						"stroke # 1.3 round: M8 4.8 V8.3 L10.4 9.8";
				case EditorIcon::Properties:
					return "stroke + 2: M2 3 H14 M2 8 H14 M2 13 H14; clear: rect 7 1 6 4 rect 3 6 6 4 rect 6 11 6 4; "
						"fill #: rect 8 1 4 4 1.2 rect 4 6 4 4 1.2 rect 7 11 4 4 1.2";
				case EditorIcon::EyeOff:
					return "stroke G 1.5 round: M1 8 Q8 0.5 15 8 Q8 15.5 1 8 Z; stroke g 1 round: M1.4 8 Q8 1.1 14.6 8 Q8 14.9 1.4 8 Z; "
						"fill g: circle 8 8 2.7; stroke o 3 round: M2.7 2.2 L13.8 13.3; stroke r 1.5 round: M2.7 2.2 L13.8 13.3";
				case EditorIcon::PlayOutline:
					return "outline 1; "
						"fill n: M3 2.6 Q3 1.4 4.1 2 L14.4 7.4 Q15.4 8 14.4 8.6 L4.1 14 Q3 14.6 3 13.4 Z M4.6 4.05 L4.6 11.95 L12.13 8 Z; "
						"fill m: M3 8 V2.6 Q3 1.4 4.1 2 L14.4 7.4 Q14.9 7.66 14.9 8 Z M4.6 4.05 L4.6 8 L12.13 8 Z";
				case EditorIcon::Passthrough:
					return "fill #: rect 2 3 2 12; fill #: M1.5 14 H4.5 V15 H5.5 V16 H0.5 V15 H1.5 Z; fill #: M4 4 H8.8 L8.4 5.6 L7.5 7.4 "
						"L8 8.6 L8.3 9.6 L8.6 10.2 L7.6 10.55 L6.4 10.5 L5.4 10 L4 9.5 Z; fill #: M9.75 0 L10.9 0 L12.1 1.5 L13.1 2.5 "
						"L13.25 3.5 L13.9 4.3 L14 5.5 L14.1 6.5 L14.5 7.4 L15.5 8.3 L15.5 8.9 L15.25 9.6 L14.25 10.5 L12.9 11.5 L12.4 12 "
						"L11 12 L10.9 11.5 L10.6 10.5 L9.9 9.5 L9.25 8.6 L9.1 7.5 L9.15 6.2 L9.6 6 L11 5.8 L11.1 4.5 L10.75 3.5 L10.25 2.5 "
						"L9.75 1.5 Z; clear: M11.2 4 H13.8 L13 5 V8.4 Q12.8 9.4 12.1 10 H11.2 Q10.9 9 11.1 7.4 Q11.4 6.6 12 6.2 V5 Z; "
						"fill # 0.88: M11.2 4 H13.8 L13 5 V8.4 Q12.8 9.4 12.1 10 H11.2 Q10.9 9 11.1 7.4 Q11.4 6.6 12 6.2 V5 Z";
				case EditorIcon::OverriddenAssets:
					return "stroke # 1 0.5: M7.5 5.5 V1.5 H1.5 V7.5 H5.5; stroke # 1: rect 7.5 7.5 7 7 0.8; fill #: circle 11 11 1.8; "
						"stroke # 1.3 round: M10.4 1.2 Q12.9 1.2 12.9 3.8; fill #: M10.8 3.6 H15 L12.9 6.4 Z";
				case EditorIcon::InvertSelection:
					return "stroke # 1: M1.5 3 V2.5 A1 1 0 0 1 2.5 1.5 H3 M13 1.5 H13.5 A1 1 0 0 1 14.5 2.5 V3 M14.5 13 V13.5 A1 1 0 0 1 13.5 14.5 H13 M3 14.5 H2.5 A1 1 0 0 1 1.5 13.5 V13 M4 1.5 H6 M7 1.5 H9 M10 1.5 H12 M4 14.5 H6 M7 14.5 H9 M10 14.5 H12 M1.5 4 V6 M1.5 7 V9 M1.5 10 V12 M14.5 4 V6 M14.5 7 V9 M14.5 10 V12; "
						"stroke # 1: rect 4.5 4.5 7 7; fill #: M11.5 4.5 V11.5 H4.5 Z";
				case EditorIcon::ScreenFrame:
					return "stroke # 1: rect 1.5 2.5 13 8 0.8; "
						"fill #: circle 11 5 1.1 M2.5 10.5 L7 5.3 L9.6 8.3 L10.6 7.2 L13.5 10.5 Z rect 7 11 2 2 rect 4 13 8 1.2 0.5";
				case EditorIcon::GrabBlock:
					return "stroke # 1 square: M1.5 4.5 V1.5 H4.5 M14.5 11.5 V14.5 H11.5; "
						"fill #: rect 4 4 3 3 rect 9 4 3 3 rect 4 9 3 3 rect 9 9 3 3";
				case EditorIcon::PauseLayers:
					return "fill # 0.25: M1.5 4.5 L7 1.7 L12.5 4.5 L7 7.3 Z; stroke # 1.1 round-join: M1.5 4.5 L7 1.7 L12.5 4.5 L7 7.3 Z; "
						"stroke # 1.1 round: M1.5 7.6 L7 10.4 L12.5 7.6; fill #: rect 10 10 2 5 rect 13 10 2 5";
				case EditorIcon::Import: return "fill #: M7 1.8 H9 V6 H11.8 L8 9.6 L4.2 6 H7 Z; stroke # 1.5 round: M3.75 9.6 V12.75 H12.25 V9.6";
				case EditorIcon::Recovery:
					return "stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; "
						"stroke # 1: M5 6.5 H11 M5 8.5 H11 M5 10.5 H11 M5 12.5 H11; clear: circle 11.5 11.5 4.9; "
						"stroke # 2.4 0.45: circle 11.5 11.5 2.8; "
						"stroke # 2.4: M10.32 8.96 A2.8 2.8 0 0 1 12.68 8.96 M14.04 10.32 A2.8 2.8 0 0 1 14.04 12.68 M12.68 14.04 A2.8 2.8 0 0 1 10.32 14.04 M8.96 12.68 A2.8 2.8 0 0 1 8.96 10.32";
				case EditorIcon::CommandPalette:
					return "fill # 0.15: rect 1.5 3.5 13 9 1; stroke # 1: rect 1.5 3.5 13 9 1; "
						"stroke # 1.5 round: M4.3 5.6 L6.1 8 L4.3 10.4; stroke # 1: M8 9.5 H12";
				case EditorIcon::RaceFlag:
					return "fill # 0.4: M5 2 H11 Q14 2.2 14 4 V5 Q14 6.5 12 7.5 Q10 8.8 8 8.5 Q6.5 8.2 5 8 Z; "
						"fill #: rect 5 2 2 2 rect 9 2 2 2 rect 7 4 2 2 rect 11 4 2 2 rect 5 6 2 2 rect 9 6 2 2; fill #: rect 3 1 2 13; "
						"fill #: M1.4 15 Q1.8 13 3 13 H5 Q6.2 13 6.6 15 Z";
				case EditorIcon::LightBulb:
					return "fill # 0.25: M6.2 10 C6.2 8.8 4.9 8.3 4.9 6.6 A3.1 3.1 0 0 1 11.1 6.6 C11.1 8.3 9.8 8.8 9.8 10 Z; "
						"stroke # 1.1 round-join: M6.2 10.2 C6.2 8.8 4.9 8.3 4.9 6.6 A3.1 3.1 0 0 1 11.1 6.6 C11.1 8.3 9.8 8.8 9.8 10.2; "
						"fill #: rect 6 10 4 1.3 0.4; fill # 0.45: M6.3 11.6 H9.7 V13.4 Q9.7 15 8 15 Q6.3 15 6.3 13.4 Z; "
						"stroke # 1.4 round: M1.4 6.6 H3 M13 6.6 H14.6; "
						"stroke # 1.4 0.55 round: M8 0.9 V2 M3.4 1.6 L4.8 2.6 M12.6 1.6 L11.2 2.6";
				case EditorIcon::EventsOnTop:
					return "fill # 0.25: M2.5 9.7 L8 7 L13.5 9.7 L8 12.4 Z; stroke # 1.1 round-join: M2.5 9.7 L8 7 L13.5 9.7 L8 12.4 Z; "
						"stroke # 1.1 round: M2.5 12.6 L8 15.3 L13.5 12.6; fill #: M7 1 H12.4 L13 2 V4 L12 5 H8.2 V9.6 H7 Z";
				case EditorIcon::Weapon:
					return "fill o: M0.9 12.8 V10.8 L1.8 8.8 L7.8 3 L9.8 1.8 L11.8 0.9 H15.2 Q16.7 1.4 16.4 3 L15.8 4.4 L11.8 8.6 V10.2 L9.8 10.6 V14.4 L9.2 15.6 H6.8 L6 14.4 L5.8 13 L3.8 13.8 H1.8 Z; "
						"fill o: M0.1 12 L5 11 L5.6 14.6 L4.6 15.4 L3.8 16 H2 L0.8 14.6 Z; "
						"fill G: M0.1 12 V10 L1 8 L7 2.2 L9 1 L11 0.1 H14.4 Q15.9 0.6 15.6 2.2 L15 3.6 L11 7.8 V9.4 L9 9.8 V13.6 L8.4 14.8 H6 L5.2 13.6 L5 12.2 L3 13 H1 Z; "
						"stroke g 2.5 round: M1.7 11.1 L5 6.9; stroke g 1.4 round: M6.4 4.4 L9.4 6.4; "
						"stroke o 1: M6.3 6.3 L8.7 8.7 M11.6 5 L10.4 6.6 M3.4 12.6 L6.6 10.2; stroke g 1 round: M6.5 12.3 L7.5 13.5; "
						"fill g: circle 12 2.6 1.9; fill w: circle 12.2 2.7 0.95; outline 1; "
						"fill n: M11 11.4 Q11 10.6 11.8 11 L14.6 12.6 Q15.2 13 14.6 13.4 L11.8 15 Q11 15.4 11 14.6 Z; "
						"stroke N 0.8 round: M11.9 14.6 L14.4 13.2; stroke m 0.8 round: M11.6 11.6 L13 12.4";
				case EditorIcon::Pencil:
					return "stroke # 2.2: M11.3 3 L10 4.3 M9.1 5.2 L4.4 9.9; stroke # 2.2 0.45: M10 4.3 L9.1 5.2; "
						"fill #: M3.62 9.12 L5.18 10.68 L2.1 12.2 Z";
				case EditorIcon::LevelFile:
					return "fill # 0.12: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z; "
						"stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; fill #: M9.5 1.5 L12.5 4.5 H9.5 Z; "
						"fill #: circle 6.3 5.8 1.3; "
						"fill #: M3.5 10.4 Q5.4 8.4 7.4 10 Q8.6 10.9 9.8 10.2 Q11.2 9.4 12.5 9.8 V14.5 H3.5 Z";
				case EditorIcon::ScriptedLevelFile:
					return "fill # 0.12: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z; "
						"stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; fill #: M9.5 1.5 L12.5 4.5 H9.5 Z; "
						"fill #: circle 6.3 5.8 1.3; "
						"fill #: M3.5 10.4 Q5.4 8.4 7.4 10 Q8.6 10.9 9.8 10.2 Q11.2 9.4 12.5 9.8 V14.5 H3.5 Z; "
						"clear: rect 6.9 6.9 10 10 2.4; fill #: rect 8 7.9 8 8.1 1.8; "
						"clear: M9 10.4 L9.85 9.55 L12.3 12 L9.85 14.45 L9 13.6 L10.6 12 Z; clear: rect 12.4 13.4 2.2 1.1";
				case EditorIcon::BrokenFile:
					return "fill # 0.12: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z; "
						"stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; fill #: M9.5 1.5 L12.5 4.5 H9.5 Z; "
						"stroke # 1 0.6: M4.6 6.4 H9.4 M4.6 8.5 H9.4; clear: rect 6.9 6.9 10 10 2.4; fill #: rect 8 7.9 8 8.1 1.8; "
						"clear: M10.82 9.98 L14.02 13.18 L13.18 14.02 L9.98 10.82 Z; "
						"clear: M13.18 9.98 L14.02 10.82 L10.82 14.02 L9.98 13.18 Z";
				case EditorIcon::TileSetFile:
					return "fill # 0.12: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z; "
						"stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; fill #: M9.5 1.5 L12.5 4.5 H9.5 Z; "
						"fill # 0.35: rect 5 7 6 6 0.8; fill #: M5 7.8 Q5 7 5.8 7 H8 V10 H5 Z M8 10 H11 V12.2 Q11 13 10.2 13 H8 Z";
				case EditorIcon::MusicFile:
					return "stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; stroke # 1.1: M8.5 5 V11; "
						"stroke # 1 round: M8.6 5.5 Q10.6 5.6 10.6 7.6; fill #: ellipse 7.1 11.4 1.9 1.3";
				case EditorIcon::TrueColorTileSetFile:
					return "fill # 0.12: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z; "
						"stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; fill #: M9.5 1.5 L12.5 4.5 H9.5 Z; "
						"fill # 0.31: rect 5 7 6 6; fill # 0.19: M5 7 H11 V11 L9 13 H5 Z; fill # 0.21: M5 7 H11 V9 L7 13 H5 Z; "
						"fill # 0.3: M5 7 H11 L5 13 Z; fill # 0.39: M5 7 H9 L5 11 Z; fill # 0.68: M5 7 H7 L5 9 Z";
				default: return nullptr;
			}
		}
	}

	void EditorIcons::Register()
	{
		UI::Windows::Icons::Register(EditorIcon::Brush, std::uint16_t(std::uint16_t(EditorIcon::CustomEnd) - std::uint16_t(EditorIcon::Brush)), &GetArt);
	}

	UI::Windows::IconArt EditorIcons::GetArt(UI::Windows::IconId icon)
	{
		UI::Windows::IconArt art;
		art.Vector = GetVectorArt(EditorIcon(icon.GetValue()));
		return art;
	}
}

#endif
