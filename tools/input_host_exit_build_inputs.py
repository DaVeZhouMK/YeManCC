"""Shared fixed MSVC build and observed-input proof for the owned exit fixtures."""
import hashlib
import json
import os
from pathlib import Path
import re
import sys

AUTH = 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
FIXED_PROFILE = 'tools/ymcc_sim/global/controller-host-exit/tool-profile.json'


def need(value, message):
    if not value:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixed_path(value):
    path = Path(value)
    need(path.is_absolute() and path.resolve(strict=True) == path and
         not (getattr(path.lstat(), 'st_file_attributes', 0) & 0x400),
         'Fixed Native input is relative or redirected: ' + str(path))
    return path


def fixed_pin(value):
    path = fixed_path(value)
    need(path.is_file(), 'Fixed Native input is not a file: ' + str(path))
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data), 'sha256': sha(data)}


def fixed_build_environment(source, run):
    need(sys.flags.isolated and sys.flags.no_site and sys.flags.dont_write_bytecode and
         sys.flags.utf8_mode and 'site' not in sys.modules, 'Fixed Native build requires isolated Python')
    profile = json.loads(source[FIXED_PROFILE])
    need(profile.get('schemaVersion') == 1 and profile.get('kind') == 'controller-host-exit-tool-profile' and
         profile.get('nativeAuthoritySha256') == sha(source[AUTH]), 'Fixed Native profile/authority mismatch')
    authority = json.loads(source[AUTH])
    native = authority['native']
    pins = {}
    for row in (*authority['files'], *profile['checkedLibraries']):
        path = Path(row['path'])
        need(path.is_absolute() and path not in pins and re.fullmatch('[0-9a-f]{64}', row['sha256']),
             'Invalid or duplicate fixed Native tool declaration: ' + str(path))
        pins[path] = row['sha256']
    tools = {role: fixed_pin(native[role]) for role in ('compiler', 'linker', 'dumpbin', 'commandProcessor')}
    for row in tools.values():
        need(pins.get(Path(row['path'])) == row['sha256'], 'Fixed Native tool bytes changed: ' + row['path'])
    compiler = Path(tools['compiler']['path'])
    need(Path(tools['linker']['path']).parent == compiler.parent, 'Fixed linker is not beside compiler')
    include, lib = tuple([fixed_path(value) for value in native[key]] for key in ('include', 'lib'))
    sdk = {m[1] for p in include + lib if (m := re.search(r'\\(?:Include|Lib)\\(\d+\.\d+\.\d+\.\d+)\\', str(p), re.I))}
    need(len(sdk) == 1, 'Expected exactly one fixed Native SDK version')
    windows = Path(tools['commandProcessor']['path']).parent.parent
    temp = run / 'temp'
    temp.mkdir()
    env = dict(SystemRoot=str(windows), windir=str(windows), ComSpec=tools['commandProcessor']['path'],
               TEMP=str(temp), TMP=str(temp), VSLANG='1033', WindowsSDKVersion=sdk.pop(),
               PATH=os.pathsep.join(map(str, (compiler.parent, windows / 'System32', windows))),
               INCLUDE=os.pathsep.join(map(str, include)), LIB=os.pathsep.join(map(str, lib)))
    return compiler, env, pins, include, lib, tools


def fixed_build_graph(cpp_pin, exe, deps, log, stdout, allowed_local, required_local, pins, include, lib):
    graph = json.loads(deps.read_text(encoding='utf-8-sig'))['Data']
    observed_tu = fixed_pin(graph['Source'])
    need(Path(observed_tu['path']) == Path(cpp_pin['path']) and
         (observed_tu['bytes'], observed_tu['sha256']) == (cpp_pin['bytes'], cpp_pin['sha256']),
         'Fixed Native translation unit changed')
    headers = []
    seen_local = set()
    for value in graph['Includes']:
        row = fixed_pin(value)
        path = Path(row['path'])
        if path in allowed_local:
            need(row['sha256'] == allowed_local[path],
                 'Fixed Native local header bytes changed: ' + str(path))
            seen_local.add(path)
        else:
            need(any(path.is_relative_to(root) for root in include) and pins.get(path) == row['sha256'],
                 'Unbound fixed Native header: ' + str(path))
        headers.append(row)
    need(required_local <= seen_local, 'Fixed Native local header set changed')
    searches = [line.strip() for line in stdout.splitlines() if line.strip().startswith('Searching ')
                and line.strip() != 'Searching libraries']
    need(searches and 'Finished searching libraries' in stdout, 'Missing fixed Native link search trace')
    libraries = {}
    for line in searches:
        match = re.fullmatch(r'Searching (.+\.lib):', line, re.I)
        need(match is not None, 'Unreadable fixed Native link search: ' + line)
        path = Path(match[1])
        if path not in libraries:
            row = fixed_pin(path)
            need(any(path.is_relative_to(root) for root in lib) and pins.get(path) == row['sha256'],
                 'Unbound fixed Native library: ' + str(path))
            libraries[path] = row
    return {'translationUnit': cpp_pin, 'binary': fixed_pin(exe), 'dependencies': fixed_pin(deps),
            'buildLog': fixed_pin(log), 'headers': headers, 'linkSearchLibraries': list(libraries.values())}


class FixedNativeBuild:
    def __init__(self, source, run):
        self.compiler, self.env, self.pins, self.include, self.lib, self.tools = fixed_build_environment(source, run)
        self.receipt = {'profileSha256': sha(source[FIXED_PROFILE]), 'tools': self.tools,
                        'environment': self.env, 'graphs': [], 'scenarioBinariesLaunched': 0}
        self.compiler_receipt = {**self.tools['compiler'], 'sdkVersion': self.env['WindowsSDKVersion'],
                                 'vcvarsExecuted': False}

    def compile(self, execute, argv, folder, cpp, exe, profile, route, allowed_local, required_local, timeout):
        command = list(argv)
        need(Path(command[0]) == self.compiler and required_local <= allowed_local.keys(), 'Fixed Native recipe mismatch')
        link = command.index('/link') if '/link' in command else len(command)
        deps = folder / 'deps.json'
        if '/sourceDependencies' in command:
            need(Path(command[command.index('/sourceDependencies') + 1]) == deps, 'Fixed Native dependency path mismatch')
        else:
            command[link:link] = ['/sourceDependencies', deps]
        if '/link' not in command:
            command.append('/link')
        command.append('/VERBOSE:LIB')
        log, saved = folder / 'build.log', folder / 'COMPILE-ARGS.json'
        cpp_pin = fixed_pin(cpp)
        with saved.open('x', encoding='utf-8') as file:
            json.dump([str(arg) for arg in command], file, indent=2)
        result = execute(command, folder, self.env, log, timeout)
        need(result.returncode == 0, 'Fixed Native build failed: ' + str(log))
        graph = fixed_build_graph(cpp_pin, exe, deps, log, result.stdout, allowed_local, required_local,
                                  self.pins, self.include, self.lib)
        graph.update(profile=profile, route=route, command=fixed_pin(saved))
        self.check_tools()
        graph_file = folder / 'FIXED-BUILD-GRAPH.json'
        with graph_file.open('x', encoding='utf-8') as file:
            json.dump(graph, file, ensure_ascii=False, indent=2)
        self.receipt['graphs'].append(fixed_pin(graph_file))
        return graph['binary']

    def check_tools(self):
        for tool in self.tools.values():
            need(fixed_pin(tool['path']) == tool, 'Fixed Native tool bytes changed: ' + tool['path'])

    def admit_runtime(self, binary):
        need(fixed_pin(binary['path']) == binary, 'Fixed Native binary changed before test execution')
        self.receipt['scenarioBinariesLaunched'] += 1

