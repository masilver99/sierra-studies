#include "sierrachart.h"
#include <cmath>

SCDLLName("Bar Numbering Advanced")

namespace
{
	enum InputIds
	{
		INPUT_USE_START_DATETIME = 0,
		INPUT_START_DATETIME = 1,
		INPUT_PIXEL_OFFSET = 2,
		INPUT_TEXT_SIZE = 3,
		INPUT_TEXT_COLOR = 4,
		INPUT_TRANSPARENT_BG = 5
	};
}

SCSFExport scsf_BarNumberingAdvanced(SCStudyInterfaceRef sc)
{
	if (sc.SetDefaults)
	{
		sc.GraphName = "Bar Numbering Advanced";
		sc.StudyDescription = "Numbers bars with a configurable start time and a fixed pixel offset below each bar.";
		sc.AutoLoop = 0;
		sc.GraphRegion = 0;
		sc.UpdateAlways = 1;

		sc.Input[INPUT_USE_START_DATETIME].Name = "Use Start Date-Time";
		sc.Input[INPUT_USE_START_DATETIME].SetYesNo(false);

		sc.Input[INPUT_START_DATETIME].Name = "Start Date-Time";
		sc.Input[INPUT_START_DATETIME].SetDateTime(SCDateTime(0.0));

		sc.Input[INPUT_PIXEL_OFFSET].Name = "Pixels Below Bar Low";
		sc.Input[INPUT_PIXEL_OFFSET].SetInt(12);
		sc.Input[INPUT_PIXEL_OFFSET].SetIntLimits(0, 200);

		sc.Input[INPUT_TEXT_SIZE].Name = "Text Size";
		sc.Input[INPUT_TEXT_SIZE].SetInt(8);
		sc.Input[INPUT_TEXT_SIZE].SetIntLimits(6, 30);

		sc.Input[INPUT_TEXT_COLOR].Name = "Text Color";
		sc.Input[INPUT_TEXT_COLOR].SetColor(RGB(255, 255, 255));

		sc.Input[INPUT_TRANSPARENT_BG].Name = "Transparent Text Background";
		sc.Input[INPUT_TRANSPARENT_BG].SetYesNo(true);

		return;
	}

	int& baseLineNumber = sc.GetPersistentInt(1);
	if (baseLineNumber == 0)
		baseLineNumber = 1100000;

	int& lastUseStart = sc.GetPersistentInt(2);
	double& lastStartDateTime = sc.GetPersistentDouble(1);

	const bool useStartDateTime = sc.Input[INPUT_USE_START_DATETIME].GetYesNo() != 0;
	const SCDateTime startDateTime = sc.Input[INPUT_START_DATETIME].GetDateTime();
	const double startDateTimeValue = useStartDateTime ? startDateTime.GetAsDouble() : 0.0;

	if (lastUseStart != (useStartDateTime ? 1 : 0) || lastStartDateTime != startDateTimeValue)
	{
		for (int i = 0; i < sc.ArraySize; ++i)
			sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + i, 0);

		lastUseStart = useStartDateTime ? 1 : 0;
		lastStartDateTime = startDateTimeValue;
	}

	if (sc.LastCallToFunction)
	{
		for (int i = 0; i < sc.ArraySize; ++i)
			sc.DeleteACSChartDrawing(sc.ChartNumber, baseLineNumber + i, 0);
		return;
	}

	int startIndex = 0;
	if (useStartDateTime && startDateTimeValue != 0.0)
	{
		startIndex = sc.ArraySize;
		for (int i = 0; i < sc.ArraySize; ++i)
		{
			if (sc.BaseDateTimeIn[i] >= startDateTime)
			{
				startIndex = i;
				break;
			}
		}
	}

	const int pixelOffset = sc.Input[INPUT_PIXEL_OFFSET].GetInt();
	const int textSize = sc.Input[INPUT_TEXT_SIZE].GetInt();
	const COLORREF textColor = sc.Input[INPUT_TEXT_COLOR].GetColor();
	const bool transparentBg = sc.Input[INPUT_TRANSPARENT_BG].GetYesNo() != 0;

	for (int i = 0; i < sc.ArraySize; ++i)
	{
		const int lineNumber = baseLineNumber + i;
		if (i < startIndex || startIndex >= sc.ArraySize)
		{
			sc.DeleteACSChartDrawing(sc.ChartNumber, lineNumber, 0);
			continue;
		}

		const float low = sc.Low[i];
		const int lowPixel = sc.RegionValueToYPixelCoordinate(low, sc.GraphRegion);
		const int targetPixel = lowPixel + pixelOffset;
		double targetValue = sc.YPixelCoordinateToGraphValue(targetPixel);

		if (!std::isfinite(targetValue) || std::fabs(targetValue) > 1e9)
			targetValue = low - sc.TickSize * 2.0f;

		SCString text;
		text.Format("%d", (i - startIndex) + 1);

		s_UseTool tool;
		tool.Clear();
		tool.ChartNumber = sc.ChartNumber;
		tool.DrawingType = DRAWING_TEXT;
		tool.AddMethod = UTAM_ADD_OR_ADJUST;
		tool.LineNumber = lineNumber;
		tool.BeginIndex = i;
		tool.BeginValue = static_cast<float>(targetValue);
		tool.Color = textColor;
		tool.TextColor = textColor;
		tool.Text = text;
		tool.FontSize = textSize;
		tool.AddAsUserDrawnDrawing = 0;
		tool.UseRelativeVerticalValues = 0;
		tool.TransparentLabelBackground = transparentBg ? 1 : 0;
		tool.Region = sc.GraphRegion;
		sc.UseTool(tool);
	}
}
