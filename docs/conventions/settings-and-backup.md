# Settings And Backup

Long-lived settings live in the settings window, not as direct rows in the
title-bar or tray menus. The menu keeps immediate window commands; the settings
window owns language, autostart, and automatic data backup.

## Settings Window

The settings window follows the same self-drawn modal style as theme management.
Shared Direct2D and DirectWrite drawing basics live in `ThemedWindowControls`;
do not duplicate local copies of font-format creation, fill, rounded-rect,
stroke, text, divider, or path-elision helpers in each settings-like window.

Keep the settings surface compact. Rows are for direct state changes and short
status values. Do not add explanatory paragraphs, nested cards, or duplicate
menu entries for the same setting.

## Automatic Backup

Automatic backup is a file mirror for the main data file only:

```text
<backup directory>\data.json
```

The source is `%APPDATA%\x-todo\data.json`. Custom themes under
`%APPDATA%\x-todo\themes\` are outside the v1 backup scope.

The persisted setting is `ui.backupDir`; an empty value means disabled.
`ui.backupLastEpoch` stores the UTC epoch seconds of the last successful
backup. Setting a directory triggers one immediate backup, then the app checks
for an hourly due backup from the main window timer. Failed background backups
do not interrupt the user; the status is shown the next time settings is open,
and the timestamp is not advanced.

Backup writes must go through a temporary file and replace the target
`data.json` atomically. The backup directory must not be the current data
directory, because that would copy the file onto itself.

The v1 feature does not include restore, versioned snapshots, zip files, backup
interval settings, or theme-directory backup.
