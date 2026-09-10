function Component() {
    component.addStopProcessForUpdateRequest(K230.process);
}

function desktopValue(value) {
    return value.replace(/\\/g, "\\\\");
}

function execArgument(value) {
    value = value.replace(/%/g, "%%").replace(/\\/g, "\\\\")
        .replace(/"/g, '\\"').replace(/`/g, "\\`").replace(/\$/g, "\\$");
    return desktopValue('"' + value + '"');
}

Component.prototype.createOperations = function() {
    var target = installer.value("TargetDir");
    if (/[\r\n\t]/.test(target)) {
        throw new Error("The installation path contains control characters.");
    }
    component.createOperations();
    // The application creates this log after installation.
    component.registerPathForUninstallation(target + "/app/bin/burning_tool.html");
    if (installer.value("CreateShortcuts", "true") === "false") {
        return;
    }
    var executable = target + "/" + K230.executable;
    if (K230.platform === "windows") {
        component.addOperation("CreateShortcut", executable,
            "@StartMenuDir@/" + K230.name + ".lnk", "workingDirectory=@TargetDir@/app/bin");
        component.addOperation("CreateShortcut", executable,
            "@DesktopDir@/" + K230.name + " (Installed).lnk", "workingDirectory=@TargetDir@/app/bin");
    } else {
        var command = "/bin/sh -c " + execArgument('exec "$1"') + " ifw-app " + execArgument(executable);
        component.addOperation("CreateDesktopEntry", K230.id + "-Installed.desktop",
            "Type=Application\nName=" + K230.name + " (Installed)\nExec=" + command +
            "\nIcon=" + desktopValue(target + "/app-icon.png") +
            "\nTerminal=false\nCategories=Development;\n");
    }
};
