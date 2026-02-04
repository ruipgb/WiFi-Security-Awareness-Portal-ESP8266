# ESP8266 Captive Portal Awareness Project

This project implements an educational captive portal using an ESP8266 microcontroller, designed to demonstrate how easily users can be deceived into submitting credentials on unsecured or malicious Wi-Fi networks.

The goal of this project is **security awareness and education**, highlighting the risks associated with open networks and fake login portals.

---

## 📌 Project Overview

The system creates a rogue Wi-Fi access point that redirects all connected clients to a captive portal.  
Upon connection, users are presented with an initial landing page (`index.html`) where they can select a login page that mimics well-known services.

After selecting a page, the user is redirected to a simulated login interface where credentials can be entered.  
All interactions are logged locally on the device for **demonstration and analysis purposes only**.

An authenticated admin panel allows real-time monitoring of connected devices, captured credentials, statistics, and logs.

---

## ⚠️ Ethical Disclaimer

> **This project is strictly for educational and awareness purposes.**  
> It is intended to demonstrate common attack vectors used in malicious captive portals and to promote better security practices.  
>  
> **Do not deploy this system on real networks or against users without explicit authorization.**

---

## ⚙️ System Features

- Captive portal with automatic redirection (DNS spoofing)
- Initial landing page with selectable login portals
- Multiple simulated login pages (HTML-based)
- Credential capture and local storage
- Real-time admin panel with authentication
- Device tracking (IP, MAC, activity, credentials)
- Rate limiting and brute-force protection
- Stealth mode (optional redirection to external site)
- Export of captured data (TXT and CSV)
- Persistent storage using LittleFS

---

## 🧠 System Logic

1. The ESP8266 starts in **Access Point (AP) mode**.
2. A DNS server redirects all requests to the captive portal.
3. Clients are served an initial page (`index.html`) to choose a login portal.
4. The selected login page is displayed to the user.
5. Submitted credentials are:
   - Stored locally in the filesystem
   - Associated with the client device
   - Logged with timestamps and metadata
6. An admin panel (HTTP Basic Auth) allows:
   - Monitoring connected devices
   - Viewing credentials and logs
   - Exporting data
   - Clearing stored information
7. Optional stealth behavior redirects users after submission.

---

## 🧩 Hardware Components

- ESP8266-based development board
- External power supply (USB / power bank)
- Client devices (for testing and demonstration)

---

## 🗂️ Project Structure

Captive-Portal/
├── src/
│ └── main.cpp
├── data/
│ ├── index.html
│ ├── facebook.html
│ ├── instagram.html
│ ├── twitter.html
│ ├── millennium.html
│ └── amazon.html
├── README.md

---

## 🛠️ Development Details

- Language: C++ (Arduino), HTML, CSS
- Platform: ESP8266
- Framework: Arduino Core for ESP8266
- Build System: PlatformIO
- Web Server: ESP8266WebServer
- DNS Redirection: DNSServer
- Filesystem: LittleFS
- Development Environment: Visual Studio Code (VS Code) with PlatformIO

---

## 📊 Admin Panel Capabilities

- Authenticated access (HTTP Basic Auth)
- Live statistics:
  - Connected devices
  - Captured credentials
  - Conversion rate
  - Most used login page
- Device table with:
  - IP address
  - MAC address
  - Activity status
  - Credential count
- Credential and log visualization
- Data export (TXT / CSV)
- Data and log reset

---

## 🔐 Security Mechanisms

- Admin authentication with rate limiting
- Blocking after repeated failed login attempts
- Session logging with timestamps
- Optional stealth redirection
- Separation between public and admin endpoints

---

## 🎓 Academic Context

This project was developed in an academic context related to:
- Network security
- Wireless attacks and defenses
- Human factors in cybersecurity
- Embedded systems and web services
- Ethical hacking and awareness training

It serves as a practical demonstration of how **trust in network names and portals can be exploited**.

---

## 🚀 Future Improvements

- HTTPS support (self-signed certificates)
- WPA2/WPA3 enterprise simulation
- External log forwarding
- Captive portal detection evasion analysis
- Integration with monitoring dashboards

---

