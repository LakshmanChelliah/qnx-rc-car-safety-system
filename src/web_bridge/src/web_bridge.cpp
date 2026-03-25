#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

// Pull in IPC and sensor definitions from the common include folder.  The
// Makefile adds ../common/include to the include search path, so these
// headers can be included by name.
#include "ipc.h"
#include "ultrasonic.h"

/**
 * Simple HTTP bridge for the QNX RC car.
 *
 * This server listens on port 8080 and translates HTTP GET requests into
 * QNX IPC messages understood by the drive controller.  It also exposes
 * an endpoint to read the latest ultrasonic sensor data from shared memory.
 *
 * Endpoints:
 *   /set_source?source=2    -> sets the active control source (SOURCE_WEBAPP)
 *   /drive?left=0.5&right=-0.5 -> sends a DriveSpeedCommandMsg
 *   /estop?engage=1         -> engages (1) or clears (0) the emergency stop
 *   /status                 -> returns JSON with ultrasonic distance and validity
 *   /                       -> serves a simple HTML control page
 *
 * The implementation avoids heavy parsing and uses simple sscanf calls to
 * extract query parameters.  It is intentionally minimalist to minimise
 * dependencies on the QNX environment.
 */

static int drive_coid = -1;

// Lazily connect to the drive controller IPC channel.  Returns true if
// the connection is available, false otherwise.  This allows the HTTP
// server to start even when the drive controller is not yet running.
static bool ensure_drive_connection() {
    if (drive_coid != -1) return true;
    drive_coid = name_open(IPC_DRIVE_CHANNEL, 0);
    if (drive_coid == -1) {
        std::cerr << "[web_bridge] drive controller not available yet" << std::endl;
        return false;
    }
    std::cout << "[web_bridge] Connected to drive controller" << std::endl;
    return true;
}

// Send a drive speed command via QNX IPC
static bool send_drive_speed(float left, float right) {
    if (!ensure_drive_connection()) return false;
    DriveSpeedCommandMsg msg{};
    msg.msg_type = MSG_TYPE_DRIVE_SPEED;
    msg.source = SOURCE_WEBAPP;
    msg.left_speed = left;
    msg.right_speed = right;
    return MsgSend(drive_coid, &msg, sizeof(msg), nullptr, 0) != -1;
}

// Change the active control source
static bool send_control_source(uint16_t src) {
    if (!ensure_drive_connection()) return false;
    DriveControlCommandMsg msg{};
    msg.msg_type = MSG_TYPE_DRIVE_CONTROL;
    msg.new_source = src;
    return MsgSend(drive_coid, &msg, sizeof(msg), nullptr, 0) != -1;
}

// Engage or clear the emergency stop
static bool send_estop(bool engage) {
    if (!ensure_drive_connection()) return false;
    EmergencyStopCommandMsg msg{};
    msg.msg_type = MSG_TYPE_EMERGENCY_STOP;
    msg.engage = engage ? 1 : 0;
    return MsgSend(drive_coid, &msg, sizeof(msg), nullptr, 0) != -1;
}

// Lazily map the ultrasonic shared memory region (read-only).  Since the
// UltrasonicNode writes using single assignments, reads may see stale data
// but will not crash if unprotected.  For production, lock the mutex at
// the offset computed in UltrasonicNode::alignedMutexOffset().
static bool read_ultrasonic(uint32_t &dist_cm, uint8_t &valid) {
    static int shm_fd = -1;
    static UltrasonicSharedState *state = nullptr;
    if (!state) {
        shm_fd = shm_open(ULTRASONIC_SHM_NAME, O_RDONLY, 0);
        if (shm_fd == -1) return false;
        state = (UltrasonicSharedState *)mmap(nullptr, sizeof(UltrasonicSharedState),
                                              PROT_READ, MAP_SHARED, shm_fd, 0);
        if (state == MAP_FAILED) {
            state = nullptr;
            return false;
        }
    }
    dist_cm = state->last_distance_cm;
    valid = state->valid;
    return true;
}

// Read the last N lines from the crash detection log and return them as a
// JSON array of strings.  Tries the primary path first, then the fallback.
static void send_crash_log(int fd, int max_lines = 20) {
    const char *paths[] = {"/fs/apps/crash_detection.log", "/tmp/crash_detection.log"};
    FILE *fp = nullptr;
    for (auto p : paths) {
        fp = std::fopen(p, "r");
        if (fp) break;
    }
    std::string json = "{\"lines\":[";
    if (fp) {
        std::vector<std::string> lines;
        char linebuf[512];
        while (std::fgets(linebuf, sizeof(linebuf), fp)) {
            size_t len = std::strlen(linebuf);
            while (len > 0 && (linebuf[len-1] == '\n' || linebuf[len-1] == '\r')) linebuf[--len] = '\0';
            if (len > 0) lines.push_back(std::string(linebuf, len));
        }
        std::fclose(fp);
        int start = (int)lines.size() - max_lines;
        if (start < 0) start = 0;
        for (int i = start; i < (int)lines.size(); i++) {
            if (i > start) json += ",";
            json += "\"";
            for (char c : lines[i]) {
                if (c == '"') json += "\\\"";
                else if (c == '\\') json += "\\\\";
                else json += c;
            }
            json += "\"";
        }
    }
    json += "]}";
    char header[256];
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
                  json.size());
    ::write(fd, header, std::strlen(header));
    ::write(fd, json.c_str(), json.size());
}

// Send an HTTP response with plain text or JSON content
static void send_response(int fd, const char *body, const char *content_type = "text/plain") {
    char header[256];
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n",
                  content_type, std::strlen(body));
    ::write(fd, header, std::strlen(header));
    ::write(fd, body, std::strlen(body));
}

// Serve the HTML control page with a virtual joystick for driving.
static void send_control_page(int fd) {
    static const char *html =
        "<!doctype html>\n"
        "<html><head><meta charset='utf-8'><title>RC Car Control</title>\n"
        "<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>\n"
        "<style>\n"
        "*{box-sizing:border-box;margin:0;padding:0;}\n"
        "body{font-family:'Segoe UI',sans-serif;background:#1a1a2e;color:#e0e0e0;\n"
        "     display:flex;flex-direction:column;align-items:center;min-height:100vh;\n"
        "     touch-action:none;user-select:none;-webkit-user-select:none;}\n"
        "h1{margin:18px 0 6px;font-size:1.4em;color:#00d4ff;}\n"
        ".status-bar{display:flex;gap:18px;align-items:center;margin:8px 0 12px;\n"
        "            font-size:0.95em;}\n"
        ".status-bar span{background:#16213e;padding:6px 14px;border-radius:8px;}\n"
        "#distance{color:#0f0;font-weight:bold;}\n"
        "#conn{color:#ff0;}\n"
        ".btn-row{display:flex;gap:10px;margin-bottom:14px;}\n"
        ".btn{padding:12px 22px;border:none;border-radius:10px;font-size:1em;\n"
        "     cursor:pointer;font-weight:600;transition:transform .1s;}\n"
        ".btn:active{transform:scale(0.93);}\n"
        ".btn-take{background:#0077ff;color:#fff;}\n"
        ".btn-estop{background:#ff2244;color:#fff;font-size:1.1em;}\n"
        ".btn-release{background:#555;color:#fff;}\n"
        ".joystick-zone{position:relative;width:280px;height:280px;\n"
        "               border-radius:50%;background:radial-gradient(#2a2a4a,#16213e);\n"
        "               border:3px solid #334;margin:10px 0;touch-action:none;}\n"
        ".joystick-base{position:absolute;top:50%;left:50%;width:120px;height:120px;\n"
        "               margin:-60px 0 0 -60px;border-radius:50%;\n"
        "               background:radial-gradient(#445,#223);border:2px solid #556;}\n"
        ".joystick-knob{position:absolute;top:50%;left:50%;width:70px;height:70px;\n"
        "               margin:-35px 0 0 -35px;border-radius:50%;\n"
        "               background:radial-gradient(#00d4ff,#0077aa);\n"
        "               box-shadow:0 0 18px rgba(0,180,255,0.5);}\n"
        ".speed-info{font-size:0.85em;color:#889;margin:6px 0 4px;}\n"
        ".crash-panel{width:90%;max-width:420px;margin:14px 0;}\n"
        ".crash-header{display:flex;align-items:center;justify-content:space-between;\n"
        "              background:#16213e;padding:10px 16px;border-radius:10px 10px 0 0;}\n"
        ".crash-header h2{font-size:1em;color:#ff6644;margin:0;}\n"
        ".crash-badge{display:inline-block;background:#332;color:#ff6644;font-size:0.75em;\n"
        "             padding:2px 8px;border-radius:10px;margin-left:8px;}\n"
        ".btn-crash{padding:8px 18px;border:none;border-radius:8px;font-size:0.9em;\n"
        "           cursor:pointer;font-weight:600;}\n"
        ".btn-crash-on{background:#ff6644;color:#fff;}\n"
        ".btn-crash-off{background:#334;color:#889;}\n"
        ".crash-alert{display:none;background:#3a1010;border:2px solid #ff2244;\n"
        "             border-radius:10px;padding:14px 18px;margin:10px 0;width:90%;\n"
        "             max-width:420px;text-align:center;animation:pulseAlert 1s infinite;}\n"
        ".crash-alert h3{color:#ff4444;font-size:1.1em;margin:0 0 6px;}\n"
        ".crash-alert p{color:#ddd;font-size:0.9em;margin:0 0 10px;}\n"
        ".crash-alert .crash-dir{font-size:1.8em;margin:4px 0;}\n"
        ".btn-clear-crash{background:#ff8800;color:#fff;border:none;border-radius:8px;\n"
        "                 padding:12px 30px;font-size:1.05em;cursor:pointer;font-weight:700;}\n"
        ".btn-clear-crash:active{transform:scale(0.95);}\n"
        "@keyframes pulseAlert{0%,100%{box-shadow:0 0 8px rgba(255,34,68,0.3);}\n"
        "  50%{box-shadow:0 0 20px rgba(255,34,68,0.6);}}\n"
        ".crash-log{background:#0d1117;border:1px solid #334;border-top:none;\n"
        "           border-radius:0 0 10px 10px;max-height:260px;overflow-y:auto;\n"
        "           padding:6px 10px;}\n"
        ".crash-log .no-data{color:#556;font-style:italic;font-size:0.85em;padding:10px;}\n"
        ".crash-card{background:#111827;border-left:3px solid #ff6644;border-radius:0 6px 6px 0;\n"
        "            margin:5px 0;padding:8px 12px;}\n"
        ".crash-card.svc{border-left-color:#3388ff;}\n"
        ".crash-card-top{display:flex;justify-content:space-between;align-items:center;}\n"
        ".crash-dir-label{font-size:1em;font-weight:700;}\n"
        ".crash-dir-label .arrow{font-size:1.3em;margin-right:4px;}\n"
        ".crash-dir-front .arrow{color:#ff4444;}\n"
        ".crash-dir-back .arrow{color:#ff8844;}\n"
        ".crash-dir-left .arrow{color:#ffaa00;}\n"
        ".crash-dir-right .arrow{color:#ffaa00;}\n"
        ".crash-time{font-size:0.75em;color:#889;}\n"
        ".crash-data{font-size:0.72em;color:#667;margin-top:4px;font-family:monospace;}\n"
        ".crash-svc-msg{font-size:0.82em;color:#88aaff;}\n"
        "@media(min-width:600px){.joystick-zone{width:340px;height:340px;}\n"
        " .joystick-base{width:150px;height:150px;margin:-75px 0 0 -75px;}\n"
        " .joystick-knob{width:90px;height:90px;margin:-45px 0 0 -45px;}}\n"
        "</style></head><body>\n"
        "<h1>RC Car Web Controller</h1>\n"
        "<div class='status-bar'>\n"
        "  <span>Distance: <b id='distance'>--</b> cm</span>\n"
        "  <span id='conn'>Connecting...</span>\n"
        "</div>\n"
        "<div class='btn-row'>\n"
        "  <button class='btn btn-take' onclick=\"fetch('/set_source?source=2').then(()=>{document.getElementById('conn').textContent='Web Control Active';document.getElementById('conn').style.color='#0f0';})\">Take Control</button>\n"
        "  <button class='btn btn-release' onclick=\"fetch('/set_source?source=0').then(()=>{document.getElementById('conn').textContent='Released';document.getElementById('conn').style.color='#ff0';})\">Release</button>\n"
        "</div>\n"
        "<div class='btn-row'>\n"
        "  <button class='btn btn-estop' id='estopBtn' onclick='toggleEstop()'>EMERGENCY STOP</button>\n"
        "</div>\n"
        "<div class='joystick-zone' id='jzone'>\n"
        "  <div class='joystick-base'>\n"
        "    <div class='joystick-knob' id='knob'></div>\n"
        "  </div>\n"
        "</div>\n"
        "<div class='speed-info'>L: <span id='vL'>0.00</span> &nbsp; R: <span id='vR'>0.00</span></div>\n"
        "<div class='crash-alert' id='crashAlert'>\n"
        "  <h3>CRASH DETECTED</h3>\n"
        "  <div class='crash-dir' id='crashDir'></div>\n"
        "  <p id='crashAlertMsg'>Impact detected — car stopped</p>\n"
        "  <button class='btn-clear-crash' onclick='clearCrashStop()'>CLEAR CRASH STOP</button>\n"
        "</div>\n"
        "<div class='crash-panel'>\n"
        "  <div class='crash-header'>\n"
        "    <h2>Crash Detection <span class='crash-badge' id='crashCount'>0 events</span></h2>\n"
        "    <button class='btn-crash btn-crash-off' id='crashBtn' onclick='toggleCrash()'>OFF</button>\n"
        "    <button class='btn-crash' id='crashClearBtn' onclick='clearCrash()' style='display:none;background:#ff6644;color:#fff;margin-left:6px;'>CLEAR</button>\n"
        "  </div>\n"
        "  <div class='crash-log' id='crashLog'><div class='no-data'>Crash monitoring off</div></div>\n"
        "</div>\n"
        "<script>\n"
        "(function(){\n"
        "  var zone=document.getElementById('jzone'),knob=document.getElementById('knob');\n"
        "  var vL=document.getElementById('vL'),vR=document.getElementById('vR');\n"
        "  var distEl=document.getElementById('distance');\n"
        "  var estopEngaged=false,activeId=null;\n"
        "  var cx=0,cy=0,maxR=0,curLeft=0,curRight=0,sendTimer=null;\n"
        "\n"
        "  function recalc(){var r=zone.getBoundingClientRect();\n"
        "    cx=r.left+r.width/2;cy=r.top+r.height/2;maxR=r.width/2-20;}\n"
        "  recalc();window.addEventListener('resize',recalc);\n"
        "  window.addEventListener('scroll',recalc);\n"
        "\n"
        "  function clamp(v,lo,hi){return v<lo?lo:v>hi?hi:v;}\n"
        "\n"
        "  function move(px,py){\n"
        "    var dx=px-cx,dy=py-cy;\n"
        "    var dist=Math.sqrt(dx*dx+dy*dy);\n"
        "    if(dist>maxR){dx=dx/dist*maxR;dy=dy/dist*maxR;dist=maxR;}\n"
        "    knob.style.transform='translate('+dx+'px,'+dy+'px)';\n"
        "    var fwd=clamp(-dy/maxR,-1,1);\n"
        "    var turn=clamp(dx/maxR,-1,1);\n"
        "    curLeft=clamp(fwd+turn,-1,1);\n"
        "    curRight=clamp(fwd-turn,-1,1);\n"
        "    vL.textContent=curLeft.toFixed(2);\n"
        "    vR.textContent=curRight.toFixed(2);\n"
        "  }\n"
        "\n"
        "  function resetKnob(){\n"
        "    knob.style.transform='translate(0px,0px)';\n"
        "    curLeft=0;curRight=0;\n"
        "    vL.textContent='0.00';vR.textContent='0.00';\n"
        "    sendDrive();\n"
        "  }\n"
        "\n"
        "  function sendDrive(){\n"
        "    fetch('/drive?left='+curLeft.toFixed(3)+'&right='+curRight.toFixed(3));\n"
        "  }\n"
        "\n"
        "  zone.addEventListener('pointerdown',function(e){\n"
        "    if(activeId!==null)return;\n"
        "    activeId=e.pointerId;zone.setPointerCapture(e.pointerId);\n"
        "    recalc();move(e.clientX,e.clientY);\n"
        "    sendTimer=setInterval(sendDrive,80);\n"
        "  });\n"
        "  zone.addEventListener('pointermove',function(e){\n"
        "    if(e.pointerId!==activeId)return;\n"
        "    move(e.clientX,e.clientY);\n"
        "  });\n"
        "  function endJoy(e){\n"
        "    if(e.pointerId!==activeId)return;\n"
        "    activeId=null;clearInterval(sendTimer);sendTimer=null;\n"
        "    resetKnob();\n"
        "  }\n"
        "  zone.addEventListener('pointerup',endJoy);\n"
        "  zone.addEventListener('pointercancel',endJoy);\n"
        "\n"
        "  window.toggleEstop=function(){\n"
        "    estopEngaged=!estopEngaged;\n"
        "    fetch('/estop?engage='+(estopEngaged?1:0));\n"
        "    var b=document.getElementById('estopBtn');\n"
        "    if(estopEngaged){b.textContent='CLEAR E-STOP';b.style.background='#ff8800';}\n"
        "    else{b.textContent='EMERGENCY STOP';b.style.background='#ff2244';}\n"
        "  };\n"
        "\n"
        "  function engageEstopSilent(){\n"
        "    if(!estopEngaged){\n"
        "      estopEngaged=true;\n"
        "      fetch('/estop?engage=1');\n"
        "      var b=document.getElementById('estopBtn');\n"
        "      b.textContent='CLEAR E-STOP';b.style.background='#ff8800';\n"
        "    }\n"
        "  }\n"
        "  function clearEstopSilent(){\n"
        "    if(estopEngaged){\n"
        "      estopEngaged=false;\n"
        "      fetch('/estop?engage=0');\n"
        "      var b=document.getElementById('estopBtn');\n"
        "      b.textContent='EMERGENCY STOP';b.style.background='#ff2244';\n"
        "    }\n"
        "  }\n"
        "\n"
        "  var crashStopped=false;\n"
        "  window.clearCrashStop=function(){\n"
        "    crashStopped=false;\n"
        "    document.getElementById('crashAlert').style.display='none';\n"
        "    clearEstopSilent();\n"
        "  };\n"
        "\n"
        "  var dirArrows={Front:'\\u2B07',Back:'\\u2B06',Left:'\\u27A1',Right:'\\u2B05'};\n"
        "  var dirLabels={Front:'Front Impact',Back:'Rear Impact',Left:'Left Impact',Right:'Right Impact'};\n"
        "\n"
        "  function showCrashAlert(direction){\n"
        "    var alert=document.getElementById('crashAlert');\n"
        "    var arrow=dirArrows[direction]||'\\u26A0';\n"
        "    var label=dirLabels[direction]||'Impact';\n"
        "    document.getElementById('crashDir').textContent=arrow;\n"
        "    document.getElementById('crashAlertMsg').textContent=label+' detected \\u2014 car stopped';\n"
        "    alert.style.display='block';\n"
        "    engageEstopSilent();\n"
        "    crashStopped=true;\n"
        "  }\n"
        "\n"
        "  function updateStatus(){\n"
        "    fetch('/status').then(function(r){return r.json();}).then(function(d){\n"
        "      distEl.textContent=d.valid?d.distance_cm:'--';\n"
        "    }).catch(function(){});\n"
        "  }\n"
        "  setInterval(updateStatus,250);updateStatus();\n"
        "\n"
        "  var crashOn=false,crashTimer=null,lastCrashCount=0,seenCrashLines=0;\n"
        "  window.toggleCrash=function(){\n"
        "    crashOn=!crashOn;\n"
        "    var b=document.getElementById('crashBtn');\n"
        "    var log=document.getElementById('crashLog');\n"
        "    var clrBtn=document.getElementById('crashClearBtn');\n"
        "    if(crashOn){\n"
        "      b.textContent='ON';b.className='btn-crash btn-crash-on';\n"
        "      clrBtn.style.display='inline-block';\n"
        "      log.innerHTML='<div class=no-data>Waiting for data...</div>';\n"
        "      lastCrashCount=0;seenCrashLines=-1;\n"
        "      fetchCrashLog();\n"
        "      crashTimer=setInterval(fetchCrashLog,1200);\n"
        "    }else{\n"
        "      b.textContent='OFF';b.className='btn-crash btn-crash-off';\n"
        "      clrBtn.style.display='none';\n"
        "      if(crashTimer){clearInterval(crashTimer);crashTimer=null;}\n"
        "      log.innerHTML='<div class=no-data>Crash monitoring off</div>';\n"
        "      document.getElementById('crashCount').textContent='0 events';\n"
        "    }\n"
        "  };\n"
        "  window.clearCrash=function(){\n"
        "    if(crashOn) toggleCrash();\n"
        "  };\n"
        "\n"
        "  function parseCrashLine(raw){\n"
        "    var m=raw.match(/Crash detected from (\\w+)/);\n"
        "    if(m){\n"
        "      var dir=m[1];\n"
        "      var tm=raw.match(/\\[wall ([^\\]]+)\\]/);\n"
        "      var up=raw.match(/\\[uptime ([^\\]]+)\\]/);\n"
        "      var ax=raw.match(/ax=([\\-\\d.]+)/);\n"
        "      var ay=raw.match(/ay=([\\-\\d.]+)/);\n"
        "      var az=raw.match(/az=([\\-\\d.]+)/);\n"
        "      var dm=raw.match(/dmag=([\\-\\d.]+)/);\n"
        "      return {type:'crash',dir:dir,\n"
        "        time:tm?tm[1]:'',uptime:up?up[1]:'',\n"
        "        ax:ax?ax[1]:'',ay:ay?ay[1]:'',az:az?az[1]:'',dmag:dm?dm[1]:''};\n"
        "    }\n"
        "    return {type:'svc',text:raw};\n"
        "  }\n"
        "\n"
        "  function renderCrashCard(p){\n"
        "    if(p.type==='svc'){\n"
        "      return '<div class=\\\"crash-card svc\\\"><span class=crash-svc-msg>'+p.text+'</span></div>';\n"
        "    }\n"
        "    var arrow=dirArrows[p.dir]||'?';\n"
        "    var cls='crash-dir-'+(p.dir||'').toLowerCase();\n"
        "    var h='<div class=crash-card><div class=crash-card-top>';\n"
        "    h+='<span class=\\\"crash-dir-label '+cls+'\\\"><span class=arrow>'+arrow+'</span> '+p.dir+' Impact</span>';\n"
        "    h+='<span class=crash-time>'+p.time+'</span></div>';\n"
        "    if(p.ax) h+='<div class=crash-data>ax='+p.ax+' ay='+p.ay+' az='+p.az+' | force='+p.dmag+'</div>';\n"
        "    h+='</div>';\n"
        "    return h;\n"
        "  }\n"
        "\n"
        "  function fetchCrashLog(){\n"
        "    fetch('/crash_log').then(function(r){return r.json();}).then(function(d){\n"
        "      var log=document.getElementById('crashLog');\n"
        "      if(!d.lines||d.lines.length===0){\n"
        "        log.innerHTML='<div class=no-data>No crash events recorded</div>';\n"
        "        document.getElementById('crashCount').textContent='0 events';\n"
        "        return;\n"
        "      }\n"
        "      var crashes=0,html='',latestDir=null;\n"
        "      for(var i=0;i<d.lines.length;i++){\n"
        "        var p=parseCrashLine(d.lines[i]);\n"
        "        html+=renderCrashCard(p);\n"
        "        if(p.type==='crash'){crashes++;latestDir=p.dir;}\n"
        "      }\n"
        "      document.getElementById('crashCount').textContent=crashes+' event'+(crashes!==1?'s':'');\n"
        "      var isNew=d.lines.length!==lastCrashCount;\n"
        "      lastCrashCount=d.lines.length;\n"
        "      log.innerHTML=html;\n"
        "      if(isNew) log.scrollTop=log.scrollHeight;\n"
        "      if(isNew && crashes>0 && seenCrashLines!==-1 && crashes>seenCrashLines && latestDir){\n"
        "        showCrashAlert(latestDir);\n"
        "      }\n"
        "      if(seenCrashLines===-1) seenCrashLines=crashes;\n"
        "      else seenCrashLines=crashes;\n"
        "    }).catch(function(){});\n"
        "  }\n"
        "})();\n"
        "</script></body></html>";
    char header[256];
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n",
                  std::strlen(html));
    ::write(fd, header, std::strlen(header));
    ::write(fd, html, std::strlen(html));
}

// Parse a float query parameter from a path of the form /drive?left=0.5&right=0.3
static bool parse_float_param(const std::string &path, const char *name, float &outVal) {
    auto pos = path.find(name);
    if (pos == std::string::npos) return false;
    pos += std::strlen(name);
    char *endptr = nullptr;
    outVal = std::strtof(path.c_str() + pos, &endptr);
    return endptr != path.c_str() + pos;
}

// Parse an integer query parameter
static bool parse_int_param(const std::string &path, const char *name, int &outVal) {
    auto pos = path.find(name);
    if (pos == std::string::npos) return false;
    pos += std::strlen(name);
    char *endptr = nullptr;
    outVal = static_cast<int>(std::strtol(path.c_str() + pos, &endptr, 10));
    return endptr != path.c_str() + pos;
}

// Handle a single HTTP request on the connected client socket
static void handle_request(int fd) {
    char buf[1024] = {0};
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    std::string req(buf);
    // Extract the request line: GET /path HTTP/1.1
    auto getPos = req.find("GET ");
    if (getPos == std::string::npos) {
        return;
    }
    auto pathStart = getPos + 4;
    auto pathEnd = req.find(' ', pathStart);
    if (pathEnd == std::string::npos) {
        return;
    }
    std::string path = req.substr(pathStart, pathEnd - pathStart);
    // Route based on path
    if (path.rfind("/drive", 0) == 0) {
        float left = 0.0f, right = 0.0f;
        parse_float_param(path, "left=", left);
        parse_float_param(path, "right=", right);
        send_drive_speed(left, right);
        send_response(fd, "OK", "text/plain");
    } else if (path.rfind("/set_source", 0) == 0) {
        int src = 0;
        parse_int_param(path, "source=", src);
        send_control_source(static_cast<uint16_t>(src));
        send_response(fd, "OK", "text/plain");
    } else if (path.rfind("/estop", 0) == 0) {
        int val = 0;
        parse_int_param(path, "engage=", val);
        send_estop(val != 0);
        send_response(fd, "OK", "text/plain");
    } else if (path == "/status") {
        uint32_t dist = 0;
        uint8_t valid = 0;
        read_ultrasonic(dist, valid);
        char body[64];
        std::snprintf(body, sizeof(body), "{\"distance_cm\":%u,\"valid\":%u}", dist, valid);
        send_response(fd, body, "application/json");
    } else if (path.rfind("/crash_log", 0) == 0) {
        send_crash_log(fd);
    } else {
        send_control_page(fd);
    }
}

int main(int argc, char *argv[]) {
    // Try to connect to the drive controller now, but don't fail if it
    // isn't running yet -- we'll retry lazily when a command is sent.
    drive_coid = name_open(IPC_DRIVE_CHANNEL, 0);
    if (drive_coid == -1) {
        std::cerr << "[web_bridge] Drive controller not running yet; "
                     "commands will be forwarded once it starts." << std::endl;
    }
    // Create a TCP socket
    int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        std::perror("[web_bridge] socket");
        return 1;
    }
    // Allow reuse of the address to avoid bind failures on restart
    int opt = 1;
    ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);
    if (::bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        std::perror("[web_bridge] bind");
        ::close(sockfd);
        return 1;
    }
    if (::listen(sockfd, 4) == -1) {
        std::perror("[web_bridge] listen");
        ::close(sockfd);
        return 1;
    }
    std::cout << "[web_bridge] Listening on port 8080..." << std::endl;
    // Main accept loop.  Each request is handled sequentially; because the
    // car control messages are small and infrequent this is sufficient.  For
    // higher throughput consider spawning a thread per client or using select().
    while (true) {
        int client_fd = ::accept(sockfd, nullptr, nullptr);
        if (client_fd == -1) {
            continue;
        }
        handle_request(client_fd);
        ::close(client_fd);
    }
    return 0;
}