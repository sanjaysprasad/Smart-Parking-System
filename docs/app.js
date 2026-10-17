// ==========================================
// FIREBASE REALTIME CLOUD IOT CONFIGURATION
// ==========================================
const firebaseConfig = {
  apiKey: "AIzaSyCipevBn0qX0SoHIOIWys8WSKp9-nRp5Xo",
  authDomain: "smart-parking-system-5d991.firebaseapp.com",
  databaseURL: "https://smart-parking-system-5d991-default-rtdb.firebaseio.com",
  projectId: "smart-parking-system-5d991",
  storageBucket: "smart-parking-system-5d991.firebasestorage.app",
  messagingSenderId: "156227017915",
  appId: "1:156227017915:web:baa2570f31df085b315d9e"
};

// Initialize Firebase
firebase.initializeApp(firebaseConfig);
const db = firebase.database();

let lastStatus = null;

// Default Admin Authentication PIN (Can be changed anytime)
const ADMIN_PIN = "admin123";

function logEvent(msg) {
  const logEl = document.getElementById("event-log");
  const time = new Date().toLocaleTimeString();
  const li = document.createElement("li");
  li.className = "log-entry";
  li.textContent = `[${time}] ${msg}`;
  logEl.insertBefore(li, logEl.firstChild);

  while (logEl.children.length > 25) {
    logEl.removeChild(logEl.lastChild);
  }
}

// ==========================================
// REAL-TIME DATABASE TELEMETRY LISTENER
// ==========================================
function listenToCloudParking() {
  const cloudPill = document.getElementById("cloud-pill");
  const cloudText = document.getElementById("cloud-text");

  // Monitor connection to Firebase servers
  db.ref(".info/connected").on("value", (snap) => {
    if (snap.val() === true) {
      cloudPill.className = "status-pill pill-online";
      cloudText.textContent = "CLOUD ONLINE";
      logEvent("Connected to Google Firebase Cloud");
    } else {
      cloudPill.className = "status-pill pill-offline";
      cloudText.textContent = "CONNECTING...";
    }
  });

  // Listen to parking metrics changes in real-time
  db.ref("parking").on("value", (snapshot) => {
    const data = snapshot.val();
    if (!data) return;

    document.getElementById("avail").textContent = data.available;
    document.getElementById("occ").textContent = data.occupied;
    document.getElementById("total").textContent = data.total;
    document.getElementById("cap-text").textContent = `${data.occupied} / ${data.total} Slots`;

    const pct = Math.min(100, Math.round((data.occupied / data.total) * 100));
    document.getElementById("bar").style.width = pct + "%";

    const banner = document.getElementById("banner");
    if (data.available > 0) {
      banner.textContent = "SLOTS AVAILABLE — PROCEED TO ENTRY";
      banner.className = "status-banner banner-ok";
    } else {
      banner.textContent = "PARKING FULL — PLEASE WAIT";
      banner.className = "status-banner banner-full";
    }

    const isEntryOpen = (data.entryGate === "OPEN" || data.entryOpen === true);
    const isExitOpen  = (data.exitGate === "OPEN" || data.exitOpen === true);

    const entryBadge = document.getElementById("entry-badge");
    entryBadge.textContent = isEntryOpen ? "OPEN" : "CLOSED";
    entryBadge.className = "badge " + (isEntryOpen ? "badge-open" : "badge-closed");

    const exitBadge = document.getElementById("exit-badge");
    exitBadge.textContent = isExitOpen ? "OPEN" : "CLOSED";
    exitBadge.className = "badge " + (isExitOpen ? "badge-open" : "badge-closed");

    // Detect transitions for the live event stream
    if (lastStatus) {
      if (!lastStatus.entryOpen && isEntryOpen) logEvent("Entry barrier raised (Gate Open)");
      if (lastStatus.entryOpen && !isEntryOpen) logEvent("Entry barrier lowered (Gate Closed)");
      if (!lastStatus.exitOpen && isExitOpen) logEvent("Exit barrier raised (Gate Open)");
      if (lastStatus.exitOpen && !isExitOpen) logEvent("Exit barrier lowered (Gate Closed)");
      if (lastStatus.occupied !== data.occupied) {
        logEvent(`Cloud occupancy updated: ${data.occupied} / ${data.total} vehicles`);
      }
    }

    lastStatus = {
      ...data,
      entryOpen: isEntryOpen,
      exitOpen: isExitOpen
    };
  });
}

// ==========================================
// PUBLIC BARRIER GATE COMMANDS
// ==========================================
function sendGateCommand(gate, action) {
  logEvent(`Cloud Command: ${gate.toUpperCase()} gate ➔ ${action.toUpperCase()}`);
  db.ref(`commands/${gate}`).set(action)
    .then(() => {
      logEvent(`Cloud dispatched: ${gate} ${action} command sent to ESP32`);
    })
    .catch((err) => {
      logEvent(`Error sending cloud command: ${err.message}`);
    });
}

// ==========================================
// ADMIN AUTHENTICATION & MANAGEMENT
// ==========================================
function openAdminModal() {
  if (sessionStorage.getItem("smartpark_admin_auth") === "true") {
    // Already logged in, toggle scroll to admin panel
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

// Check PIN on Enter key
document.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && document.getElementById("admin-modal").style.display === "flex") {
    verifyAdminPin();
  }
});

// ==========================================
// ADMIN-ONLY COMMANDS
// ==========================================
function sendSlotCommand(action) {
  if (sessionStorage.getItem("smartpark_admin_auth") !== "true") {
    alert("Admin access required.");
    return;
  }
  logEvent(`Admin action: Slot adjustment (${action})`);
  db.ref("commands/slots").set(action)
    .then(() => {
      logEvent(`Dispatched slot command: ${action}`);
    })
    .catch((err) => {
      logEvent(`Error: ${err.message}`);
    });
}

function resetSlots() {
  if (sessionStorage.getItem("smartpark_admin_auth") !== "true") {
    alert("Admin access required.");
    return;
  }
  if (!confirm("Are you sure you want to reset all parking slots to 0 occupied?")) return;
  logEvent("Admin action: Dispatched master reset to Firebase Cloud");
  db.ref("commands/reset").set(true)
    .then(() => {
      logEvent("Master reset command queued in Cloud");
    })
    .catch((err) => {
      logEvent(`Error resetting: ${err.message}`);
    });
}

// Initialize on page load
document.addEventListener("DOMContentLoaded", () => {
  logEvent("Initializing Cloud IoT Dashboard...");
  listenToCloudParking();

  // Restore admin state if already authenticated in this session
  const isAuth = (sessionStorage.getItem("smartpark_admin_auth") === "true");
  applyAdminState(isAuth);
});
