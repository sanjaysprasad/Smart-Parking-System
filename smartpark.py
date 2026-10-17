#!/usr/bin/env python3
"""
SmartPark Terminal Controller
Control your ESP32 Barrier Gates and monitor live parking from the macOS Terminal via Firebase.
"""

import sys
import time
import json
import ssl
import urllib.request

FIREBASE_URL = "https://smart-parking-system-5d991-default-rtdb.firebaseio.com"

# SSL Context for macOS Python
ctx = ssl._create_unverified_context()

# Terminal Colors
C_BLUE   = "\033[94m"
C_GREEN  = "\033[92m"
C_YELLOW = "\033[93m"
C_RED    = "\033[91m"
C_CYAN   = "\033[96m"
C_BOLD   = "\033[1m"
C_RESET  = "\033[0m"

def api_get(endpoint):
    url = f"{FIREBASE_URL}/{endpoint}.json"
    req = urllib.request.Request(url, headers={"User-Agent": "SmartPark-CLI"})
    with urllib.request.urlopen(req, context=ctx) as res:
        return json.loads(res.read().decode())

def api_put(endpoint, data):
    url = f"{FIREBASE_URL}/{endpoint}.json"
    body = json.dumps(data).encode("utf-8")
    req = urllib.request.Request(url, data=body, method="PUT", headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, context=ctx) as res:
        return json.loads(res.read().decode())

def print_banner():
    print(f"\n{C_CYAN}{C_BOLD}╔═══════════════════════════════════════════════════════╗")
    print(f"║       🚗 SMART PARKING CLOUD TERMINAL CONTROLLER      ║")
    print(f"╚═══════════════════════════════════════════════════════╝{C_RESET}")

def get_status():
    try:
        data = api_get("parking")
        if not data:
            print(f"{C_RED}Error: Could not retrieve parking data.{C_RESET}")
            return None
        return data
    except Exception as e:
        print(f"{C_RED}Connection error: {e}{C_RESET}")
        return None

def show_status(data=None):
    if data is None:
        data = get_status()
    if not data:
        return

    occ = data.get("occupied", 0)
    avail = data.get("available", 0)
    total = data.get("total", 20)
    entry_gate = data.get("entryGate", "CLOSED")
    exit_gate  = data.get("exitGate", "CLOSED")

    pct = round((occ / total) * 100) if total > 0 else 0
    bar_filled = int(pct / 5)
    bar = "█" * bar_filled + "░" * (20 - bar_filled)

    print("\n" + "─" * 45)
    print(f" {C_BOLD}PARKING LOT STATUS{C_RESET}")
    print("─" * 45)
    print(f" Available:  {C_GREEN}{C_BOLD}{avail:2d} slots{C_RESET}")
    print(f" Occupied:   {C_YELLOW}{C_BOLD}{occ:2d} slots{C_RESET}")
    print(f" Total:      {C_BLUE}{C_BOLD}{total:2d} slots{C_RESET}")
    print(f" Occupancy:  [{C_CYAN}{bar}{C_RESET}] {pct}%")
    print("─" * 45)
    
    entry_color = C_GREEN if entry_gate == "OPEN" else C_RED
    exit_color  = C_GREEN if exit_gate == "OPEN" else C_RED
    print(f" Entry Gate: {entry_color}{C_BOLD}{entry_gate}{C_RESET}")
    print(f" Exit Gate:  {exit_color}{C_BOLD}{exit_gate}{C_RESET}")
    print("─" * 45 + "\n")

def send_gate_command(gate, action):
    print(f"Sending {C_BOLD}{gate.upper()}{C_RESET} gate ➔ {C_YELLOW}{action.upper()}{C_RESET}...")
    try:
        api_put(f"commands/{gate}", action)
        print(f"{C_GREEN}✓ Command sent to Firebase Cloud successfully!{C_RESET}")
    except Exception as e:
        print(f"{C_RED}Failed to send command: {e}{C_RESET}")

def send_slot_command(action):
    print(f"Sending slot override ➔ {C_YELLOW}{action.upper()}{C_RESET}...")
    try:
        api_put("commands/slots", action)
        print(f"{C_GREEN}✓ Slot command queued in Firebase!{C_RESET}")
    except Exception as e:
        print(f"{C_RED}Failed: {e}{C_RESET}")

def reset_all_slots():
    confirm = input(f"{C_RED}Are you sure you want to reset all slots to 0? (y/N): {C_RESET}").strip().lower()
    if confirm == "y":
        print("Dispatching master reset...")
        try:
            api_put("commands/reset", True)
            print(f"{C_GREEN}✓ Master reset command queued!{C_RESET}")
        except Exception as e:
            print(f"{C_RED}Failed: {e}{C_RESET}")

def watch_live():
    print(f"\n{C_CYAN}Streaming live parking updates (Press Ctrl+C to stop)...{C_RESET}")
    try:
        last_hash = ""
        while True:
            data = get_status()
            curr_hash = json.dumps(data)
            if curr_hash != last_hash:
                print("\033[H\033[J", end="")  # Clear screen
                print_banner()
                print(f" {C_GREEN}● LIVE STREAM ACTIVE{C_RESET}  [Updated: {time.strftime('%H:%M:%S')}]")
                show_status(data)
                last_hash = curr_hash
            time.sleep(1)
    except KeyboardInterrupt:
        print(f"\n{C_YELLOW}Stream stopped.{C_RESET}\n")

def interactive_menu():
    print_banner()
    while True:
        print(f"{C_BOLD}ACTIONS:{C_RESET}")
        print(" [1] 📊 View Live Status")
        print(" [2] 🟢 Open Entry Gate (3s Pulse)")
        print(" [3] 🔓 Hold Entry Gate Open")
        print(" [4] 🔒 Close Entry Gate")
        print(" [5] 🟢 Open Exit Gate (3s Pulse)")
        print(" [6] 🔓 Hold Exit Gate Open")
        print(" [7] 🔒 Close Exit Gate")
        print(" [8] ➕ Manual Entry (+1)")
        print(" [9] ➖ Manual Exit (-1)")
        print(" [10]🔄 Reset Slots (0)")
        print(" [11]📡 Live Monitor Mode (Auto-refresh)")
        print(" [0] 🚪 Exit")

        choice = input(f"\n{C_BOLD}Select an option [0-11]: {C_RESET}").strip()
        if choice == "1":
            show_status()
        elif choice == "2":
            send_gate_command("entry", "pulse")
        elif choice == "3":
            send_gate_command("entry", "open")
        elif choice == "4":
            send_gate_command("entry", "close")
        elif choice == "5":
            send_gate_command("exit", "pulse")
        elif choice == "6":
            send_gate_command("exit", "open")
        elif choice == "7":
            send_gate_command("exit", "close")
        elif choice == "8":
            send_slot_command("inc")
        elif choice == "9":
            send_slot_command("dec")
        elif choice == "10":
            reset_all_slots()
        elif choice == "11":
            watch_live()
        elif choice == "0":
            print("Goodbye!\n")
            break
        else:
            print(f"{C_RED}Invalid option.{C_RESET}\n")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        cmd = sys.argv[1].lower()
        if cmd == "status":
            show_status()
        elif cmd == "watch" or cmd == "monitor":
            watch_live()
        elif cmd == "entry" and len(sys.argv) > 2:
            send_gate_command("entry", sys.argv[2].lower())
        elif cmd == "exit" and len(sys.argv) > 2:
            send_gate_command("exit", sys.argv[2].lower())
        elif cmd == "inc":
            send_slot_command("inc")
        elif cmd == "dec":
            send_slot_command("dec")
        elif cmd == "reset":
            reset_all_slots()
        else:
            print(f"Usage: python3 smartpark.py [status | watch | entry <pulse|open|close> | exit <pulse|open|close> | inc | dec | reset]")
    else:
        interactive_menu()
