#include "printer.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace hm {

const char* const kMoonrakerQuery =
    "/printer/objects/query?display_status=progress&print_stats=state,print_duration";

namespace {

void copyStr(char* dst, size_t n, const char* src){
    if (!src){
        return;
    }
    snprintf(dst, n, "%s", src);
}

// Temperatures arrive as strings ("220.380000"), counters as numbers
bool readFloat(JsonVariantConst v, float& out){
    if (v.isNull()){
        return false;
    }
    if (v.is<const char*>()){
        const char* s = v.as<const char*>();
        char* end = nullptr;
        const float f = strtof(s, &end);
        if (end == s){
            return false;
        }
        out = f;
        return true;
    }
    out = v.as<float>();
    return true;
}

// int and int32_t are the same type on some targets and not on others
template <typename T>
bool readInt(JsonVariantConst v, T& out){
    float f;
    if (!readFloat(v, f)){
        return false;
    }
    out = static_cast<T>(f >= 0.0f ? f + 0.5f : f - 0.5f);
    return true;
}

const JsonDocument& crealityFilter(){
    static JsonDocument f;
    static bool built = false;
    if (!built){
        built = true;
        f["state"] = true;
        f["deviceState"] = true;
        f["printProgress"] = true;
        f["printLeftTime"] = true;
        f["printJobTime"] = true;
        f["layer"] = true;
        f["TotalLayer"] = true;
        f["nozzleTemp"] = true;
        f["targetNozzleTemp"] = true;
        f["bedTemp0"] = true;
        f["targetBedTemp0"] = true;
        f["err"]["errcode"] = true;
        f["printFileName"] = true;
        f["hostname"] = true;
        f["model"] = true;
        f["ModeCode"] = true;
    }
    return f;
}

}  // namespace

const char* phaseName(Phase p){
    switch (p){
        case Phase::Unknown:   return "unknown";
        case Phase::Idle:      return "idle";
        case Phase::Preparing: return "preparing";
        case Phase::Printing:  return "printing";
        case Phase::Paused:    return "paused";
        case Phase::Stopping:  return "stopping";
        case Phase::Complete:  return "complete";
        case Phase::Failed:    return "failed";
        case Phase::Aborted:   return "aborted";
    }
    return "?";
}

bool applyCrealityMessage(PrinterStatus& st, const char* data, size_t len, bool* heartbeat){
    if (heartbeat){
        *heartbeat = false;
    }
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, data, len, DeserializationOption::Filter(crealityFilter()));
    if (e || !doc.is<JsonObjectConst>()){
        return false;
    }
    JsonObjectConst o = doc.as<JsonObjectConst>();

    const char* mode = o["ModeCode"] | static_cast<const char*>(nullptr);
    if (mode && strcmp(mode, "heart_beat") == 0){
        if (heartbeat){
            *heartbeat = true;
        }
        return true;
    }

    readInt(o["state"], st.state);
    readInt(o["deviceState"], st.deviceState);
    readInt(o["printProgress"], st.progress);
    readInt(o["printLeftTime"], st.leftS);
    readInt(o["printJobTime"], st.jobS);
    readInt(o["layer"], st.layer);
    readInt(o["TotalLayer"], st.layers);
    readFloat(o["nozzleTemp"], st.nozzle);
    readFloat(o["targetNozzleTemp"], st.nozzleTarget);
    readFloat(o["bedTemp0"], st.bed);
    readFloat(o["targetBedTemp0"], st.bedTarget);
    if (!o["err"].isNull()){
        readInt(o["err"]["errcode"], st.errorCode);
    }
    const char* path = o["printFileName"] | static_cast<const char*>(nullptr);
    if (path){
        const char* slash = strrchr(path, '/');
        copyStr(st.file, sizeof(st.file), slash ? slash + 1 : path);
    }
    copyStr(st.hostname, sizeof(st.hostname), o["hostname"] | static_cast<const char*>(nullptr));
    copyStr(st.model, sizeof(st.model), o["model"] | static_cast<const char*>(nullptr));
    st.seen = true;
    return true;
}

Phase phaseOf(const PrinterStatus& st){
    if (!st.seen){
        return Phase::Unknown;
    }
    switch (st.state){
        case 0: return Phase::Idle;
        case 1:
        case 8: return st.layer == 0 ? Phase::Preparing : Phase::Printing;
        case 9: return Phase::Preparing;
        case 2: return Phase::Complete;
        case 3: return Phase::Failed;
        case 4: return Phase::Aborted;
        case 5:
        case 6: return Phase::Paused;
        case 7: return Phase::Stopping;
        default: return Phase::Unknown;
    }
}

bool parseMoonraker(MoonrakerStatus& out, const char* data, size_t len){
    JsonDocument doc;
    if (deserializeJson(doc, data, len)){
        return false;
    }
    JsonObjectConst status = doc["result"]["status"];
    if (status.isNull()){
        return false;
    }
    MoonrakerStatus m;
    float f;
    if (readFloat(status["display_status"]["progress"], f)) m.m73 = f;
    if (readFloat(status["print_stats"]["print_duration"], f)) m.printS = f;
    copyStr(m.state, sizeof(m.state), status["print_stats"]["state"] | static_cast<const char*>(nullptr));
    m.valid = true;
    out = m;
    return true;
}

}  // namespace hm
