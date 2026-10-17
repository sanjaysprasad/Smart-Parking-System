// Smart Parking Control App Logic
let baseUrl = "";
let lastStatus = null;
let pollTimer = null;

// Default Admin Authentication PIN (Can be changed anytime)
const ADMIN_PIN = "admin@marichinthikuka";

// Determine if running standalone or directly from ESP32
function initBaseUrl() {
  const isLocalFile = window.location.protocol === "file:";
  const isExternalHost = !["192.168.4.1", "localhost", "127.0.0.1"].includes(window.location.hostname) && window.location.port !== "";
  
  const ipBar = document.getElementById("ip-config-bar");
  const ipInput = document.getElementById("esp-ip");
  const saveBtn = document.getElementById("save-ip-btn");

  const savedIp = localStorage.getItem("smartpark_ip") || "http://192.168.4.1";
  ipInput.value = savedIp;

  if (isLocalFile || isExternalHost) {
    ipBar.style.display = "flex";
    baseUrl = savedIp.replace(/\/+$/, "");
  } else {
    baseUrl = "";
  }

  saveBtn.addEventListener("click", () => {
    let val = ipInput.value.trim().replace(/\/+$/, "");
    if (!val.startsWith("http://") && !val.startsWith("https://")) {
      val = "http://" + val;
    }
    ipInput.value = val;
    localStorage.setItem("smartpark_ip", val);
    baseUrl = val;
    logEvent(`Connected target set to ${baseUrl}`);
    fetchStatus();
  });
}

function logEvent(msg) {
  const logEl = document.getElementById("event-log");
  const time = new Date().toLocaleTimeString();
  const li = document.createElement("li");
  li.className = "log-entry";
  li.textContent = `[${time}] ${msg}`;
  logEl.insertBefore(li, logEl.firstChild);

  while (logEl.children.length > 20) {
    logEl.removeChild(logEl.lastChild);
  }
}

async function fetchStatus() {
  const connPill = document.getElementById("conn-status");
  const connText = document.getElementById("conn-text");

  try {
    const res = await fetch(`${baseUrl}/status`, { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    connPill.className = "status-pill pill-online";
    connText.textContent = "ONLINE";

    document.getElementById("avail").textContent = data.available;
    document.getElementById("occ").textContent = data.occupied;
    document.getElementById("total").textContent = data.total;
    document.getElementById("cap-text").textContent = `${data.occupied} / ${data.total} Slots`;

    const pct = Math.min(100, Math.round((data.occupied / data.total) * 100));
    document.getElementById("bar").style.width = pct + "%";

    const banner = document.getElementById("banner");
    if (data.available > 0) {
      banner.textContent = "SLOTS AVAILABLE — READY FOR ENTRY";
      banner.className = "status-banner banner-ok";
    } else {
      banner.textContent = "PARKING FULL — ALL SLOTS OCCUPIED";
      banner.className = "status-banner banner-full";
    }

    const entryBadge = document.getElementById("entry-badge");
    entryBadge.textContent = data.entryOpen ? "OPEN" : "CLOSED";
    entryBadge.className = "badge " + (data.entryOpen ? "badge-open" : "badge-closed");

    const exitBadge = document.getElementById("exit-badge");
    exitBadge.textContent = data.exitOpen ? "OPEN" : "CLOSED";
    exitBadge.className = "badge " + (data.exitOpen ? "badge-open" : "badge-closed");

    if (lastStatus) {
      if (!lastStatus.entryOpen && data.entryOpen) logEvent("Entry barrier raised");
      if (lastStatus.entryOpen && !data.entryOpen) logEvent("Entry barrier lowered");
      if (!lastStatus.exitOpen && data.exitOpen) logEvent("Exit barrier raised");
      if (lastStatus.exitOpen && !data.exitOpen) logEvent("Exit barrier lowered");
      if (lastStatus.occupied !== data.occupied) {
        logEvent(`Occupancy updated: ${data.occupied}/${data.total} occupied`);
      }
    }
    lastStatus = data;
  } catch (err) {
    connPill.className = "status-pill pill-offline";
    connText.textContent = "OFFLINE";
    document.getElementById("banner").textContent = "DISCONNECTED FROM ESP32";
    document.getElementById("banner").className = "status-banner banner-full";
  }
}

// ==========================================
// PUBLIC BARRIER GATE CONTROLS
// ==========================================
async function actuateGate(gate, action) {
  logEvent(`Command: ${gate.toUpperCase()} gate -> ${action.toUpperCase()}`);
  try {
    const res = await fetch(`${baseUrl}/api/gate/${gate}?action=${action}`, { cache: "no-store" });
    const text = await res.text();
    logEvent(`Response: ${text}`);
    fetchStatus();
  } catch (err) {
    logEvent(`Error sending ${gate} gate command`);
  }
}

// ==========================================
// ADMIN AUTHENTICATION
// ==========================================
function openAdminModal() {
  if (sessionStorage.getItem("smartpark_admin_auth") === "true") {
    const panel = document.getElementById("admin-panel");
    panel.scrollIntoView({ behavior: "smooth" });
    return;
  }
  document.getElementById("admin-pin-input").value = "";
  document.getElementById("admin-err-msg").style.display = "none";
  document.getElementById("admin-modal").style.display = "flex";
  document.getElementById("admin-pin-input").focus();
}

function closeAdminModal() {
  document.getElementById("admin-modal").style.display = "none";
}

function verifyAdminPin() {
  const input = document.getElementById("admin-pin-input").value.trim();
  if (input === ADMIN_PIN) {
    sessionStorage.setItem("smartpark_admin_auth", "true");
    closeAdminModal();
    applyAdminState(true);
    logEvent("Admin authentication successful");
  } else {
    document.getElementById("admin-err-msg").style.display = "block";
  }
}

function logoutAdmin() {
  sessionStorage.removeItem("smartpark_admin_auth");
  applyAdminState(false);
  logEvent("Admin session closed");
}

function applyAdminState(isAdmin) {
  const panel = document.getElementById("admin-panel");
  const btn = document.getElementById("admin-btn");
  if (isAdmin) {
    panel.style.display = "block";
    btn.textContent = "🛡️ Admin Active";
    btn.className = "btn btn-sm btn-primary";
  } else {
    panel.style.display = "none";
    btn.textContent = "🔒 Admin";
    btn.className = "btn btn-sm btn-outline";
  }
}

document.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && document.getElementById("admin-modal").style.display === "flex") {
    verifyAdminPin();
  }
});

// ==========================================
// ADMIN-ONLY COMMANDS
// ==========================================
async function adjustSlots(action) {
  if (sessionStorage.getItem("smartpark_admin_auth") !== "true") {
    alert("Admin access required.");
    return;
  }
  logEvent(`Admin action: Slot count adjustment (${action})`);
  try {
    const res = await fetch(`${baseUrl}/api/slots?action=${action}`, { cache: "no-store" });
    const text = await res.text();
    logEvent(`Response: ${text}`);
    fetchStatus();
  } catch (err) {
    logEvent(`Error adjusting slot count`);
  }
}

async function resetSlots() {
  if (sessionStorage.getItem("smartpark_admin_auth") !== "true") {
    alert("Admin access required.");
    return;
  }
  if (!confirm("Are you sure you want to reset all slots to 0 occupied?")) return;
  logEvent("Admin action: Resetting parking slots to 0...");
  try {
    const res = await fetch(`${baseUrl}/reset`, { cache: "no-store" });
    const text = await res.text();
    logEvent(`Response: ${text}`);
    fetchStatus();
  } catch (err) {
    logEvent("Error resetting slots");
  }
}

// Initialization
document.addEventListener("DOMContentLoaded", () => {
  initBaseUrl();
  logEvent("Web Controller initialized");
  fetchStatus();
  pollTimer = setInterval(fetchStatus, 1000);

  const isAuth = (sessionStorage.getItem("smartpark_admin_auth") === "true");
  applyAdminState(isAuth);
});
