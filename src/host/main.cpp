// Host-side renderer: draws the tide clock screen into a PNG instead of onto
// the e-paper panel. Same canvas, same fonts, same layout code as the firmware,
// so the image is pixel-identical to what the X4 displays.
//
//   pio run -e sim
//   .pio/build/sim/program --hilo test/fixtures/ile_dentree_hilo.json \
//                          --wlp  test/fixtures/ile_dentree_wlp.json \
//                          --out  docs/screenshots/now.png

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "canvas.h"
#include "iwls_parse.h"
#include "render.h"
#include "time_utils.h"
#include "png_writer.h"

namespace {

struct Options {
    std::string hilo = "test/fixtures/ile_dentree_hilo.json";
    std::string wlp = "test/fixtures/ile_dentree_wlp.json";
    std::string out = "render.png";
    std::string station = "ILE D'ENTREE, QC";
    std::string banner;
    std::string message;  // when set, render the fallback screen instead
    std::string tz = "AST4ADT,M3.2.0,M11.1.0";
    int64_t now = -1;  // -1 = a day into the loaded curve
    int64_t fetchedAgo = 3600;
    int batteryPercent = 76;
    bool charging = false;
    bool hour24 = false;
};

bool readFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path.c_str());
        return false;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

void usage() {
    printf(
        "usage: program [options]\n"
        "  --hilo FILE        wlp-hilo fixture (high/low predictions)\n"
        "  --wlp FILE         wlp fixture (water level series)\n"
        "  --out FILE         output PNG (default render.png)\n"
        "  --station NAME     station name recorded in the parsed data\n"
        "  --banner TEXT      warning strip above the cards\n"
        "  --message T|A|B    render the fallback screen with these three lines\n"
        "  --now ISO8601      simulated clock, e.g. 2026-08-08T15:00:00Z\n"
        "  --tz TZSPEC        POSIX TZ string (default Atlantic)\n"
        "  --fetched-ago SEC  age of the cached download (default 3600)\n"
        "  --battery PCT      battery reading, or -1 to leave it off\n"
        "  --charging         show the battery as charging\n"
        "  --24h              use a 24-hour clock\n");
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool hasNext = i + 1 < argc;
        if (a == "--help" || a == "-h") {
            usage();
            return false;
        } else if (a == "--hilo" && hasNext) {
            o.hilo = argv[++i];
        } else if (a == "--wlp" && hasNext) {
            o.wlp = argv[++i];
        } else if (a == "--out" && hasNext) {
            o.out = argv[++i];
        } else if (a == "--station" && hasNext) {
            o.station = argv[++i];
        } else if (a == "--banner" && hasNext) {
            o.banner = argv[++i];
        } else if (a == "--message" && hasNext) {
            o.message = argv[++i];
        } else if (a == "--now" && hasNext) {
            o.now = tiParseIso8601(argv[++i]);
        } else if (a == "--tz" && hasNext) {
            o.tz = argv[++i];
        } else if (a == "--fetched-ago" && hasNext) {
            o.fetchedAgo = atoll(argv[++i]);
        } else if (a == "--battery" && hasNext) {
            o.batteryPercent = atoi(argv[++i]);
        } else if (a == "--charging") {
            o.charging = true;
        } else if (a == "--24h") {
            o.hour24 = true;
        } else {
            fprintf(stderr, "unknown option: %s\n", a.c_str());
            usage();
            return false;
        }
    }
    return true;
}

// Splits "title|line one|line two" for --message.
void splitPipes(const std::string& s, std::string out[3]) {
    size_t start = 0;
    for (int i = 0; i < 3; i++) {
        const size_t bar = s.find('|', start);
        out[i] = s.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) return argc > 1 ? 0 : 1;
    tiSetTimezone(o.tz.c_str());

    static uint8_t framebuffer[(kScreenWidth / 8) * kScreenHeight];
    Canvas canvas(framebuffer, kScreenWidth, kScreenHeight);

    if (!o.message.empty()) {
        std::string lines[3];
        splitPipes(o.message, lines);
        renderMessageScreen(canvas, lines[0].c_str(), lines[1].empty() ? nullptr : lines[1].c_str(),
                            lines[2].empty() ? nullptr : lines[2].c_str());
    } else {
        TideData data;
        tideDataReset(data);
        snprintf(data.stationName, sizeof(data.stationName), "%s", o.station.c_str());

        std::string hilo, wlp;
        if (!readFile(o.hilo, hilo) || !readFile(o.wlp, wlp)) return 1;
        if (!iwlsParseHiLo(hilo.c_str(), hilo.size(), data)) {
            fprintf(stderr, "failed to parse %s\n", o.hilo.c_str());
            return 1;
        }
        if (!iwlsParseCurve(wlp.c_str(), wlp.size(), data)) {
            fprintf(stderr, "failed to parse %s\n", o.wlp.c_str());
            return 1;
        }
        data.valid = true;

        if (o.now < 0) {
            // Default a day into the curve, so the whole tide day around it has
            // predictions on both sides.
            o.now = data.curve.startTime + 24 * 3600;
        }
        data.fetchedAt = o.now - o.fetchedAgo;

        RenderStatus st;
        st.now = o.now;
        st.hour24 = o.hour24;
        st.banner = o.banner.empty() ? nullptr : o.banner.c_str();
        st.batteryPercent = (int16_t)o.batteryPercent;
        st.charging = o.charging;

        renderTideScreen(canvas, data, st);
        printf("%s: %u extremes, %u curve points, step %us\n", o.out.c_str(), data.extremeCount,
               data.curve.count, data.curve.stepSec);
    }

    if (!png::write(o.out, canvas.buffer(), canvas.width(), canvas.height(), canvas.stride())) {
        fprintf(stderr, "failed to write %s\n", o.out.c_str());
        return 1;
    }
    return 0;
}
