# Galaxy Android v0.0.4-alpha — Tester Build Announcement & Reporting Guide

Release Tag: v0.0.4-alpha
Official Tester Discord: https://discord.gg/H3qdNWYu2
GitHub Releases: https://github.com/OpenPort-Labs/Galaxy-Android/releases
GitHub Issues: https://github.com/OpenPort-Labs/Galaxy-Android/issues

---

## Discord Tester Announcement

@everyone
Galaxy Android v0.0.4-alpha Tester Build is now available!

Download: https://github.com/OpenPort-Labs/Galaxy-Android/releases/tag/v0.0.4-alpha
Discord Link: https://discord.gg/H3qdNWYu2

### Highlights in v0.0.4-alpha:
- **Modular Hardware Graphics Architecture**: Decomposed the monolithic Vulkan renderer into 21 clean single-responsibility units (sub-300 LOC each) for maintainability and compiler optimization.
- **Cutscene Crash Safeguards**: Fixed crashes during opening and in-game cutscenes with THP video decoder bounds validation and missing-file fallbacks.
- **Star Pointer Edge Tracking**: Expanded cursor navigation to 100% of physical display bounds on 20:9 ultrawide screens.
- **Natural Camera Controls**: Fixed camera swipe direction and right-stick orbit mechanics.
- **Integrated Tester Feedback**: One-tap access to Discord and in-app tester notices.

### Mandatory Tester Instructions:
All testers are required to join our Discord server (https://discord.gg/H3qdNWYu2) for reporting feedback and issues.

When reporting issues, please provide:
1. Device model (e.g., Poco X3, Galaxy S23, Pixel 7)
2. Chipset & GPU (Snapdragon, Mali, Dimensity)
3. Android OS version
4. Log output from the in-game Settings > View System Logs Console
5. Precise steps to reproduce the issue

Please do NOT send emails for support. Email is a strict personal privacy boundary and will not be answered. All bug reports and discussions belong on Discord and GitHub.

— OpenPort Labs
