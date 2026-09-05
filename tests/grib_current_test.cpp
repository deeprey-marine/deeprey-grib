// grib_current_test — the direction convention of a current record, and whether a fine
// coastal grid can sit beside a coarse weather grid in the same loaded set.
//
// WHY THIS EXISTS. GribRecord::getInterpolatedValues returns atan2(-vx,-vy) — the
// direction the vector comes FROM. That is the meteorological convention and it is right
// for wind. Water is described the other way round: "set" is where the water is GOING, and
// DpGribPassageSample documents its field that way. Every other consumer in this plugin
// adds the 180 (see CursorData.cpp); the passage-planner sampler did not, so a fair tide
// was handed to the simulator as a foul one and the planner would have recommended the
// worst departure hour of the day with complete confidence.
//
// A sign error like that produces a plausible number, so nothing on screen looks broken.
// It only shows up as a passage plan that is quietly, consistently wrong — which is why it
// gets a test rather than a comment.
//
// Runs against the REAL GribRecord, with records built in memory rather than decoded from
// a file: the question is what the interpolator does with known components, and a file
// would only add a decoder (and jasper) between the assertion and the thing asserted.
//
// build: ./tests/build.sh

#include "wx/wx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "GribReader.h"
#include "GribRecord.h"

static int failures = 0;

static void Check(bool ok, const char* what) {
  if (!ok) {
    printf("   FAIL: %s\n", what);
    ++failures;
  }
}

// A record with a chosen grid and a uniform vector component. Subclassed because the grid
// description is protected — which is correct, a decoded record has no business being
// reshaped, and a test is the one caller that legitimately needs to write it.
class TestRecord : public GribRecord {
 public:
  TestRecord(double la1, double lo1, double di, double dj, int ni, int nj, double value) {
    Ni = ni;
    Nj = nj;
    La1 = la1;
    Lo1 = lo1;
    Di = di;
    Dj = dj;
    La2 = la1 + (nj - 1) * dj;
    Lo2 = lo1 + (ni - 1) * di;
    hasDiDj = true;
    hasBMS = false;
    ok = true;
    knownData = true;
    m_bfilled = true;
    // GribRecord's default constructor sets only m_bfilled, and the destructor frees both
    // pointers unconditionally — so anything not written here is freed as garbage.
    BMSbits = nullptr;
    BMSsize = 0;
    data = new double[static_cast<size_t>(ni) * nj];
    for (int j = 0; j < nj; ++j) {
      for (int i = 0; i < ni; ++i) data[j * ni + i] = value;
    }
  }
};

// The flip the passage sampler applies. Kept here in the same shape as the fix so the test
// fails if one changes without the other.
static double ToSet(double fromDeg) {
  double d = fromDeg + 180.0;
  if (d >= 360.0) d -= 360.0;
  if (d < 0.0) d += 360.0;
  return d;
}

static double AngleDiff(double a, double b) {
  double d = std::fabs(a - b);
  if (d > 180.0) d = 360.0 - d;
  return d;
}

// Reads the probe file to_grib.py writes beside its GRIB: known-truth samples taken off
// the NetCDF BEFORE encoding, so the round-trip is judged against the source rather than
// against itself. A file encoded upside down round-trips perfectly against itself.
struct Probe {
  double lat, lon, u, v;
  time_t when;
};

static std::vector<Probe> ReadProbes(const char* path) {
  std::vector<Probe> out;
  FILE* f = fopen(path, "r");
  if (!f) return out;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#') continue;
    Probe p;
    long long epoch = 0;
    if (sscanf(line, "%lf %lf %lld %lf %lf", &p.lat, &p.lon, &epoch, &p.u, &p.v) == 5) {
      p.when = static_cast<time_t>(epoch);
      out.push_back(p);
    }
  }
  fclose(f);
  return out;
}

int main(int argc, char** argv) {
  constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
  constexpr double kMsToKn = 1.9438444924406;

  printf("GRIB CURRENT CONVENTION\n\n");

  // AMM15's measured grid, over the Alderney box: 0.0303 deg of longitude by 0.0134 of
  // latitude, which is about 1.2 by 0.8 NM. The numbers are the ones Stage 1 came back
  // with, so the fine record here is the shape of the real thing.
  const double kFineDi = 0.0303, kFineDj = 0.0134;

  printf("1. WHAT THE INTERPOLATOR RETURNS\n");
  {
    // Water running due EAST at 1.5 m/s. u is positive eastward, v positive northward.
    TestRecord ux(49.4, -2.4, kFineDi, kFineDj, 60, 30, 1.5);
    TestRecord vy(49.4, -2.4, kFineDi, kFineDj, 60, 30, 0.0);

    double m = 0.0, a = 0.0;
    Check(GribRecord::getInterpolatedValues(m, a, &ux, &vy, -2.0, 49.7),
          "a fine-grid current record interpolates");
    printf("   water running EAST at 1.5 m/s -> magnitude %.2f kn, angle %.0f deg\n",
           m * kMsToKn, a);

    Check(std::fabs(m * kMsToKn - 2.92) < 0.02, "magnitude is the vector length in knots");
    Check(AngleDiff(a, 270.0) < 0.5,
          "the angle is where the water comes FROM, not where it goes");
    Check(AngleDiff(ToSet(a), 90.0) < 0.5, "and the flip turns it into the set");
  }

  printf("2. THE EBB DOWN THE CHANNEL COAST\n");
  {
    // The Portland case: an ebb setting WSW, 250 degrees, at 2 m/s.
    const double set = 250.0, sp = 2.0;
    TestRecord ux(50.3, -2.6, kFineDi, kFineDj, 60, 30, sp * std::sin(set * kDeg2Rad));
    TestRecord vy(50.3, -2.6, kFineDi, kFineDj, 60, 30, sp * std::cos(set * kDeg2Rad));

    double m = 0.0, a = 0.0;
    Check(GribRecord::getInterpolatedValues(m, a, &ux, &vy, -2.45, 50.5), "the ebb reads");
    printf("   set 250 at 2.0 m/s -> raw angle %.0f, flipped %.0f\n", a, ToSet(a));

    Check(AngleDiff(a, 70.0) < 0.5, "raw comes back as the reciprocal");
    Check(AngleDiff(ToSet(a), set) < 0.5, "flipped, it is the set the water is running");

    // The one that matters. A boat steering 250 in this ebb is carried ALONG its course.
    // Applied unflipped it would be stopped dead instead — same number on screen, opposite
    // passage.
    const double courseTrue = 250.0;
    auto along = [&](double dirDeg) {
      const double ch = courseTrue * kDeg2Rad, sh = dirDeg * kDeg2Rad;
      return (m * kMsToKn) * (std::sin(sh) * std::sin(ch) + std::cos(sh) * std::cos(ch));
    };
    printf("   boat steering 250: %+.2f kn with the flip, %+.2f kn without\n",
           along(ToSet(a)), along(a));
    Check(along(ToSet(a)) > 3.0, "the ebb pushes the boat along its course");
    Check(along(a) < -3.0, "and unflipped it would stop the boat dead instead");
  }

  printf("3. A FINE COASTAL GRID BESIDE A COARSE WEATHER GRID\n");
  {
    // The Stage 2 question: current at 0.0135 deg and wind at 0.25 deg in one loaded set.
    // Each GribRecord carries its own Ni/Nj/La1/Lo1/Di/Dj and interpolates on it, so the
    // two never need to agree — which is what makes the whole thing a data job rather than
    // a rewrite.
    TestRecord fineU(49.4, -2.4, kFineDi, kFineDj, 60, 30, 1.5);
    TestRecord fineV(49.4, -2.4, kFineDi, kFineDj, 60, 30, 0.0);
    TestRecord windU(45.0, -10.0, 0.25, 0.25, 80, 60, 10.0);
    TestRecord windV(45.0, -10.0, 0.25, 0.25, 80, 60, 0.0);

    const double lon = -2.0, lat = 49.7;

    double cm = 0.0, ca = 0.0, wm = 0.0, wa = 0.0;
    const bool gotCur = GribRecord::getInterpolatedValues(cm, ca, &fineU, &fineV, lon, lat);
    const bool gotWind =
        GribRecord::getInterpolatedValues(wm, wa, &windU, &windV, lon, lat);

    printf("   at %.2f/%.2f  current %.2f kn (grid %.4f deg)  wind %.1f kn (grid %.2f deg)\n",
           lat, lon, cm * kMsToKn, kFineDi, wm * kMsToKn, 0.25);

    Check(gotCur && gotWind, "both grids answer at the same point");
    Check(std::fabs(cm - 1.5) < 1e-9 && std::fabs(wm - 10.0) < 1e-9,
          "each on its own grid, neither disturbing the other");

    // And the coarse grid covers a point the fine one does not — the ordinary case, and
    // the one that must read as "no current here" rather than as calm water.
    const double offshoreLon = -8.0, offshoreLat = 47.0;
    double om = 0.0, oa = 0.0;
    Check(!GribRecord::getInterpolatedValues(om, oa, &fineU, &fineV, offshoreLon,
                                             offshoreLat),
          "outside the coastal grid the current declines to answer");
    Check(GribRecord::getInterpolatedValues(om, oa, &windU, &windV, offshoreLon,
                                            offshoreLat),
          "while the weather grid still covers it");
  }

  // 4. THE REAL FILE, END TO END. Optional because it needs a converted AMM15 download;
  //    skipped rather than failed when one is not on the box, so the convention checks
  //    above still run anywhere.
  if (argc > 2) {
    printf("4. A CONVERTED AMM15 FILE THROUGH THE PLUGIN'S OWN READER\n");

    const std::vector<Probe> probes = ReadProbes(argv[2]);
    Check(!probes.empty(), "the probe file has known-truth samples in it");

    GribReader reader;
    reader.openFile(wxString::FromUTF8(argv[1]));
    Check(reader.isOk(), "the converted file opens");

    if (reader.isOk() && !probes.empty()) {
      // Find the current records the way the plugin does: by the data type its own
      // decoder assigned, not by anything this test asserted on the way in.
      std::vector<GribRecord*> us, vs;
      for (auto& entry : *reader.getGribMap()) {
        for (GribRecord* r : *entry.second) {
          if (r->getDataType() == GRB_UOGRD) us.push_back(r);
          if (r->getDataType() == GRB_VOGRD) vs.push_back(r);
        }
      }
      printf("   decoded %zu u-records and %zu v-records\n", us.size(), vs.size());
      Check(!us.empty() && !vs.empty(),
            "the reader recognises discipline 10 / category 1 as a current");
      Check(us.size() == vs.size(), "and finds both components at every step");

      if (!us.empty()) {
        printf("   grid %d x %d at %.4f x %.4f deg\n", us[0]->getNi(), us[0]->getNj(),
               us[0]->getDi(), us[0]->getDj());
        Check(std::fabs(us[0]->getDi() - 0.0303) < 5e-4 &&
                  std::fabs(us[0]->getDj() - 0.0135) < 5e-4,
              "the coastal grid survived the encoding");
      }

      // Match each probe to the records carrying its timestamp and compare.
      int checked = 0;
      double worstSpeed = 0.0, worstAngle = 0.0;
      for (const Probe& p : probes) {
        GribRecord *gu = nullptr, *gv = nullptr;
        for (GribRecord* r : us) {
          if (r->getRecordCurrentDate() == p.when) gu = r;
        }
        for (GribRecord* r : vs) {
          if (r->getRecordCurrentDate() == p.when) gv = r;
        }
        if (!gu || !gv) continue;

        double m = 0.0, a = 0.0;
        if (!GribRecord::getInterpolatedValues(m, a, gu, gv, p.lon, p.lat)) continue;

        const double trueSpeed = std::hypot(p.u, p.v);
        double trueSet = std::atan2(p.u, p.v) / kDeg2Rad;
        if (trueSet < 0.0) trueSet += 360.0;

        worstSpeed = std::max(worstSpeed, std::fabs(m - trueSpeed));
        worstAngle = std::max(worstAngle, AngleDiff(ToSet(a), trueSet));
        ++checked;
      }

      printf("   %d probes: worst speed error %.4f m/s, worst set error %.2f deg\n",
             checked, worstSpeed, worstAngle);
      Check(checked > 0, "probes line up with decoded records by timestamp");
      // The tolerance is the GRIB's own packing, not slack: values are quantised on the
      // way in, and a centimetre per second is well under what the model itself resolves.
      Check(worstSpeed < 0.01, "speed survives NetCDF -> GRIB2 -> decoder");
      Check(worstAngle < 0.5,
            "and so does the set, once flipped out of the wind convention");
    }
  } else {
    printf("4. REAL FILE  (skipped - pass <file.grb2> <file.probe> to run it)\n");
  }

  printf("\n%s  (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
