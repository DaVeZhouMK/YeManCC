def get_system_pythonpaths() -> list:
    try:
        executable = "python3" if localplatform.ON_LINUX else "python"
        options = dict(capture_output=True,
                       user=localplatform.localplatform._get_user_id() if localplatform.ON_LINUX else None,
                       env={} if localplatform.ON_LINUX else None)
        if localplatform.ON_WINDOWS:
            import shutil
            executable = shutil.which("python")
            # An optional path probe must not activate Windows Store/manager aliases.
            if not executable:
                return []
            normalized = os.path.normcase(os.path.normpath(executable)).replace("\\", "/")
            if "/microsoft/windowsapps/" in normalized:
                return []
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = subprocess.SW_HIDE
            options.update(creationflags=subprocess.CREATE_NO_WINDOW,
                           startupinfo=startup, timeout=5)
        proc = subprocess.run([executable, "-c", "import sys; print('\\n'.join(x for x in sys.path if x))"], **options)
        proc.check_returncode()
        versions = [x.strip() for x in proc.stdout.decode().strip().split("\n")]
        return [x for x in versions if x and not x.isspace()]
    except Exception as e:
        logger.warning(f"Failed to execute get_system_pythonpaths(): {str(e)}")
        return []
