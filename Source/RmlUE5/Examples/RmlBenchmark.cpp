#include "RmlBenchmark.h"
#include "RmlUi/Core/Input.h"
#include "RmlUi/Core/Profiling.h"

DECLARE_STATS_GROUP(TEXT("RmlUI_Benchmark"), STATGROUP_RmlUI_Benchmark, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("RmlUI SetInnerRML (50 rows)"), STAT_RmlUI_SetInnerRML, STATGROUP_RmlUI_Benchmark);

void URmlBenchmark::OnInit()
{
	using namespace Rml;
	BoundDocument->GetElementById("title")->SetInnerRML("Benchmark");
}

void URmlBenchmark::OnKeyDown()
{
	auto key_identifier = (Rml::Input::KeyIdentifier)CurrentEvent->GetParameter<int>("key_identifier", 0);

	switch (key_identifier)
	{
	case Rml::Input::KI_RIGHT:
		bRunUpdate = false;
		bSingleUpdate = true;
		break;
	case Rml::Input::KI_RETURN:
		bRunUpdate = !bRunUpdate;
		break;
	}
}

void URmlBenchmark::Tick(float DeltaTime)
{
	if (!BoundDocument) return;

	if (bRunUpdate || bSingleUpdate)
	{
		bSingleUpdate = false;

		PerformanceTest();
	}

	static constexpr int buffer_size = 200;
	static float fps_buffer[buffer_size] = {};
	static int buffer_index = 0;

	static double t_prev = 0.0;
	double t = FSlateApplication::Get().GetCurrentTime();
	float dt = float(t - t_prev);
	t_prev = t;
	static int count_frames = 0;
	count_frames += 1;

	if (dt != 0)
	{
		float fps = 1.0f / dt;
		fps_buffer[buffer_index] = fps;
		buffer_index = ((buffer_index + 1) % buffer_size);
	}
	else
	{
		fps_buffer[buffer_index] = fps_buffer[buffer_index == 0 ? buffer_size - 1 : buffer_index - 1];
		buffer_index = ((buffer_index + 1) % buffer_size);
	}

	if (count_frames > buffer_size / 8)
	{
		float fps_mean = 0;
		for (int i = 0; i < buffer_size; i++)
			fps_mean += fps_buffer[(buffer_index + i) % buffer_size];
		fps_mean = fps_mean / (float)buffer_size;

		auto el = BoundDocument->GetElementById("fps");
		count_frames = 0;
		el->SetInnerRML(Rml::CreateString("FPS: %f", fps_mean));
	}
}

void URmlBenchmark::PerformanceTest()
{
	RMLUI_ZoneScoped;

	if (!BoundDocument || !bDoPerformanceTest)
		return;

	Rml::String rml;

	for (int i = 0; i < 50; i++)
	{
		int index = rand() % 1000;
		int route = rand() % 50;
		int max = (rand() % 40) + 10;
		int value = rand() % max;
		Rml::String rml_row = Rml::CreateString(
			R"(
			<div class="row">
				<div class="col col1"><button class="expand" index="%d">+</button>&nbsp;<a>Route %d</a></div>
				<div class="col col23"><input type="range" class="assign_range" min="0" max="%d" value="%d"/></div>
				<div class="col col4">Assigned</div>
				<select>
					<option>Red</option><option>Blue</option><option selected>Green</option><option style="background-color: yellow;">Yellow</option>
				</select>
				<div class="inrow unmark_collapse">
					<div class="col col123 assign_text">Assign to route</div>
					<div class="col col4">
						<input type="submit" class="vehicle_depot_assign_confirm" quantity="0">Confirm</input>
					</div>
				</div>
			</div>)",
			index,
			route,
			max,
			value
		);
		rml += rml_row;
	}

	if (auto el = BoundDocument->GetElementById("performance"))
	{
		SCOPE_CYCLE_COUNTER(STAT_RmlUI_SetInnerRML);
		el->SetInnerRML(rml);
	}
}
