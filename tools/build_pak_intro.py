"""Build the Steam 14236148 intro pak and optional loose-file package."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

PAK_NAME = 'Tchia-IntroSkip_P.pak'
MENU = 'Tchia/Content/Level/RefLevels/lvl_mainmenu.umap'
CARDS = 'Tchia/Content/Level/MainMenu/LaunchCards/launch_cards1.uasset'

TITLE_WIDGET = 'Tchia/Content/Level/MainMenu/ui_Tchiamainmenu.uasset'
SHORT = CARDS.replace('launch_cards1', 'IntroSkipCards')
CONFIG = 'Tchia/Config/Windows/WindowsGame.ini'
DLL = 'Tchia/Binaries/Win64/X3DAudio1_7.dll'
DLL_HASH = '758e774ddf4b7fed1ad2203e04eb5edaa54230e1382e09e081bb1efe11b10d8f'
PAK = 'Tchia/Content/Paks/' + PAK_NAME
GRAPH = 'ExecuteUbergraph_lvl_mainmenu'


SOURCE_HASHES = {
    "Tchia/Content/Level/MainMenu/LaunchCards/launch_cards1.uasset": "1d824cbdf798f6fa9aab01e2ec15eb9c3316dc721a5a14e5789771f8798d6776",
    "Tchia/Content/Level/MainMenu/LaunchCards/launch_cards1.uexp": "f515fd6aacbd5a033f9759444b2aa326af8bac02ebf7653cece20719074b5ee6",
    "Tchia/Content/Level/MainMenu/ui_Tchiamainmenu.uasset": "4ecaa8cc0bc50740cee1210f33e2f3021762d32af4913caef6f4e6e97ccd2701",
    "Tchia/Content/Level/MainMenu/ui_Tchiamainmenu.uexp": "315c6eb97128117be4027d347612111d547b20f7ca366f9812b2c6551ad1983a",
    "Tchia/Content/Level/RefLevels/lvl_mainmenu.uexp": "11cd951fb47c06d0acfab28e72ee484346215296ed838e47cd945f885003ed4b",
    "Tchia/Content/Level/RefLevels/lvl_mainmenu.umap": "a677ea7e1137d00eb3d9eca3f122ddeaf267e9d2b6578df9af8538275178f8ac"
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args):
    subprocess.run([str(a) for a in args], check=True, timeout=180)


def prop(data, name):
    return next(p for p in data if p['Name'] == name)


def replace(node, expression, field, old, new):
    if node['$type'].split(',')[0].rsplit('.', 1)[-1] != expression or node[field] != old:
        raise ValueError(f'Unexpected startup bytecode: {expression}.{field}')
    node[field] = new


def skip_flower(asset):
    graph = next(e['ScriptBytecode'] for e in asset['Exports']
                 if e['ObjectName'] == 'ExecuteUbergraph_lvl_mainmenu')
    # OnLaunchCardsFinished still sets the view target, sky state and fade.
    # Continue at the ambience activation (1578), past the press-any-key widget.
    replace(graph[93], 'EX_PushExecutionFlow', 'PushingAddress', 1412, 1578)
    # Reuse the original latent action to launch the native menu camera on
    # the next tick. Entry 15 is the same PlayCameraMouvement used after pickup.
    delay = graph[81]['Parameters']
    replace(delay[1], 'EX_FloatConst', 'Value', 2.5, '+0')
    replace(delay[2]['Value'][0], 'EX_SkipOffsetConst', 'Value', 336, 15)
    # Retain the native camera-ready event and menu/focus initialization.
    delay = graph[83]['Parameters']
    replace(delay[1], 'EX_FloatConst', 'Value', 2.0, '+0')
    replace(graph[89]['Expression']['Parameters'][0], 'EX_FloatConst', 'Value', 4.0, 0.25)
    # Do not arm the splash skip key after the shortened cards have ended.
    # Preload the original background ambience without the flower audio.
    replace(graph[116], 'EX_Jump', 'CodeOffset', 1282, 1391)


def skip_cards(asset):
    scene = next(e for e in asset['Exports'] if e['ObjectName'] == 'MovieScene_0')
    ranges = prop(scene['Data'], 'PlaybackRange')['Value'][0]['Value']
    lower, upper = ranges['LowerBound'], ranges['UpperBound']
    tick_rate = prop(prop(scene['Data'], 'TickResolution')['Value'], 'Numerator')['Value']
    display_rate = prop(prop(scene['Data'], 'DisplayRate')['Value'], 'Numerator')['Value']
    if (lower['Type'], lower['Value']['Value'], upper['Type'], upper['Value']['Value'],
            tick_rate, display_rate) != ('Inclusive', 0, 'Exclusive', 328000, 24000, 30):
        raise ValueError('Unexpected launch cards playback range')
    # Play the last two frames so native sequence completion and cleanup fire.
    lower['Value']['Value'] = upper['Value']['Value'] - 2 * (tick_rate // display_rate)


def pack_and_check(args, stage, staged, mount):
    files = sorted(stage.rglob('*.*'))
    if {p.relative_to(stage).as_posix() for p in files} != set(staged):
        raise ValueError('Unexpected staged package files')
    response = args.output / 'pak-files.txt'
    response.write_text('\n'.join(f'"{p}" "../../../{p.relative_to(stage).as_posix()}"'
                                  for p in files), encoding='utf-8')
    pak = args.output / PAK_NAME
    extracted = args.output / 'pak-extracted'
    run(args.unreal_pak, pak, '-create=' + str(response), '-unattended',
        '-abslog=' + str(args.output / 'pak-build.log'))
    run(args.unreal_pak, pak, '-Test', '-unattended',
        '-abslog=' + str(args.output / 'pak-test.log'))
    run(args.unreal_pak, pak, '-Extract', extracted, '-unattended',
        '-abslog=' + str(args.output / 'pak-extract.log'))
    listing = args.output / 'pak-list.log'
    run(args.unreal_pak, pak, '-List', '-unattended', '-abslog=' + str(listing))
    expected_mount = 'Mount point ../../../' + mount.relative_to(stage).as_posix() + '/'
    if not any(line.rstrip().endswith(expected_mount) for line in listing.read_text(encoding='utf-8-sig').splitlines()):
        raise ValueError('Unexpected pak mount point')
    if {p.relative_to(extracted).as_posix() for p in extracted.rglob('*.*')} != {
            p.relative_to(mount).as_posix() for p in files}:
        raise ValueError('Unexpected extracted pak paths')
    for path in files:
        if sha(path) != sha(extracted / path.relative_to(mount)):
            raise ValueError(f'Pak extraction differs: {path}')
    return pak


def export(asset, name):
    return next(e for e in asset['Exports'] if e['ObjectName'] == name)


def write_archive(archive, files):
    expected = {member: sha(path) for member, path in files.items()}
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
        for member, path in files.items():
            z.write(path, member)
    with zipfile.ZipFile(archive) as z:
        actual = {member: hashlib.sha256(z.read(member)).hexdigest() for member in z.namelist()}
        if len(z.namelist()) != len(expected) or actual != expected:
            raise ValueError(f'ZIP verification failed: {archive}')
    return {'zip': str(archive), 'sha256': sha(archive), 'entries': expected}


def short_cards(asset):
    # Retain a cooked compiled-data object with a matching signature and empty
    # evaluation masks. Every object reference is rebuilt in this private package.
    skip_cards(asset)
    sequence, scene, compiled = [export(asset, n) for n in ('launch_cards1', 'MovieScene_0', 'CompiledData')]
    sequence['ObjectName'] = 'IntroSkipCards'
    sequence['Data'] = [prop(sequence['Data'], n) for n in ('MovieScene', 'CompiledData', 'Signature')]
    sequence['Data'][0]['Value'], sequence['Data'][1]['Value'] = 2, 3
    scene['Data'] = [prop(scene['Data'], n) for n in ('PlaybackRange', 'TickResolution', 'DisplayRate', 'Signature')]
    bounds = scene['Data'][0]['Value'][0]['Value']
    ticks = bounds['UpperBound']['Value']['Value'] - bounds['LowerBound']['Value']['Value']
    bounds['LowerBound']['Value']['Value'], bounds['UpperBound']['Value']['Value'] = 0, ticks
    compiled['Data'] = [prop(compiled['Data'], n) for n in ('CompiledSignature', 'CompilerVersion')]
    if compiled['Data'][0]['Value'][0]['Value'] != sequence['Data'][2]['Value'][0]['Value'] or ticks != 1600:
        raise ValueError('Unexpected original compiled signature or playback rate')
    imports = asset['Imports']
    old_indices = [e[k] for e in (sequence, scene, compiled) for k in ('ClassIndex', 'TemplateIndex')]
    old_indices += sorted({imports[-i-1]['OuterIndex'] for i in old_indices})
    index = {old: -i for i, old in enumerate(old_indices, 1)}
    asset['Imports'] = [imports[-i-1] for i in old_indices]
    for item in asset['Imports']:
        if item['OuterIndex']:
            item['OuterIndex'] = index[item['OuterIndex']]
    asset['Exports'] = [sequence, scene, compiled]
    for e in asset['Exports']:
        e['ClassIndex'], e['TemplateIndex'] = index[e['ClassIndex']], index[e['TemplateIndex']]
        e['OuterIndex'] = 0 if e is sequence else 1
        e['SerializationBeforeCreateDependencies'] = [e['ClassIndex'], e['TemplateIndex']]
        e['CreateBeforeCreateDependencies'] = [] if e is sequence else [1]
        e['CreateBeforeSerializationDependencies'] = [2, 3] if e is sequence else []
    asset['DependsMap'] = [[], [], []]
    # These exports contain only tagged properties, no opaque FName indices.
    # Preserve base names for numbered FNames (e.g. MovieScene_0). All
    # remaining dependencies are engine script types; drop stock content paths.
    names = [n for n in asset['NameMap'] if not n.startswith(('/Game/', '/Engine/'))]
    names.append(sequence['ObjectName'])
    asset['NameMap'] = names
    asset['Generations'][0].update(ExportCount=3, NameCount=len(names))
    asset['NamesReferencedFromExportDataCount'] = len(names)


def show_title_opaque(widget):
    section = export(widget, 'MovieSceneFloatSection_0')
    curve = prop(section['Data'], 'FloatCurve')['Value'][0]['Value']
    binding = prop(export(widget, 'MovieSceneFloatTrack_0')['Data'], 'PropertyBinding')['Value']
    if (prop(binding, 'PropertyName')['Value'] != 'RenderOpacity' or
            [t['Value'] for t in curve['Times']] != [0, 60000] or
            [v['Value'] for v in curve['Values']] != ['+0', 1.0] or
            curve['TickResolution']['Numerator'] != 60000):
        raise ValueError('Unexpected title reveal track')
    graph = export(widget, 'ExecuteUbergraph_ui_Tchiamainmenu')['ScriptBytecode']
    play = graph[1]['AssignmentExpression']
    if play['Parameters'][0]['Variable']['New']['Path'] != ['Appear']:
        raise ValueError('Unexpected title animation')
    replace(play['Parameters'][1], 'EX_FloatConst', 'Value', '+0', 1.0)


def menu(asset):
    skip_flower(asset)
    actor = export(asset, 'launch_cards_2')
    path = prop(actor['Data'], 'LevelSequence')['Value'][0]['Value']['AssetPath']
    old = '/Game/Level/MainMenu/LaunchCards/launch_cards1.launch_cards1'
    if path['AssetName'] != old:
        raise ValueError('Unexpected launch-card actor')
    path['AssetName'] = old.replace('launch_cards1', 'IntroSkipCards')
    asset['NameMap'].append(path['AssetName'])
    camera = export(asset, 'MainMenu_CameraMovement_2')
    settings = prop(camera['Data'], 'PlaybackSettings')['Value']
    if [p['Name'] for p in settings] != ['bPauseAtEnd']:
        raise ValueError('Unexpected title camera playback settings')
    # CineCameraActor1's introductory rotation ends at tick 67200 (24 kHz).
    sample = next(p for e in asset['Exports'] for p in e.get('Data', []) if p['Name'] == 'CurrentAperture')
    settings.append({**sample, 'Name': 'StartTime', 'Value': 2.8125})
    # Start the placed camera at the pose used by its sequence after the pan.
    # Otherwise the level renders its frame-zero transform before Play evaluates.
    camera_actor = export(asset, 'CineCameraActor1')
    root = asset['Exports'][prop(camera_actor['Data'], 'RootComponent')['Value'] - 1]
    location = prop(root['Data'], 'RelativeLocation')['Value'][0]['Value']
    rotation = prop(root['Data'], 'RelativeRotation')['Value'][0]['Value']
    if (location['Z'], rotation['Pitch']) != (10300.0, 9.99999713897705):
        raise ValueError('Unexpected initial title camera pose')
    location['Z'], rotation['Pitch'] = 10560.0, 18.66748046875

    fn = export(asset, GRAPH)
    graph = fn['ScriptBytecode']
    material = 'm_tchia_logo_appear'
    package = '/Game/UI/OpeningCredits/Textures/' + material
    imports = asset['Imports']
    imports.append({**imports[144], 'ObjectName': package})
    imports.append({**imports[-1], 'ObjectName': material, 'ClassName': 'Material',
                    'ClassPackage': '/Script/Engine', 'OuterIndex': -len(imports)})
    logo = {**graph[76]['AssignmentExpression']['ObjectExpression'], 'Value': -len(imports)}
    fn['CreateBeforeSerializationDependencies'].append(logo['Value'])
    for name in ('StartTime', material, package, 'Material', 'SetForceMipLevelsToBeResident'):
        if name not in asset['NameMap']:
            asset['NameMap'].append(name)

    def expr(kind, **fields):
        return {'$type': f'UAssetAPI.Kismet.Bytecode.Expressions.EX_{kind}, UAssetAPI', **fields}

    def jump_if_missing(variable, address):
        test = {**graph[6]['Expression'], 'Target': variable}
        return {**graph[106], 'CodeOffset': address, 'BooleanExpression': test}

    # The ordinary BeginPlay branch has already acquired the controller.
    end = graph.pop()
    begin = len(graph)
    view = graph[103]
    controller = graph[105]['VariableExpression']
    camera_ref = view['ContextExpression']['Parameters'][0]
    request_logo = expr('VirtualFunction', VirtualFunctionName='SetForceMipLevelsToBeResident',
                        Parameters=[expr('False'), expr('False'), expr('FloatConst', Value=30.0),
                                    expr('IntZero'), expr('True')])
    graph.extend([{**graph[79], 'ObjectExpression': logo, 'Offset': 23, 'ContextExpression': request_logo},
                  jump_if_missing(controller, 26), jump_if_missing(camera_ref, 26),
                  {**view, 'ObjectExpression': controller}, expr('Jump', CodeOffset=26)])

    # The game releases its initial blackout when the short cards start.
    # Keep the 3D camera black until its title sequence and menu are ready.
    # SetManualCameraFade applies immediately, even before the next camera tick.
    engine = next(-i for i, item in enumerate(imports, 1) if item['ObjectName'] == '/Script/Engine')
    imports.append({**imports[49], 'ObjectName': 'GameplayStatics', 'OuterIndex': engine})
    imports.append({**imports[101], 'ObjectName': 'GetPlayerCameraManager', 'OuterIndex': -len(imports)})
    get_camera = -len(imports)
    fn['CreateBeforeSerializationDependencies'].append(get_camera)
    for name in ('GameplayStatics', 'GetPlayerCameraManager', 'SetManualCameraFade'):
        if name not in asset['NameMap']:
            asset['NameMap'].append(name)
    camera_manager = expr('CallMath', StackNode=get_camera, Parameters=[expr('Self'), expr('IntZero')])
    color = graph[91]['ContextExpression']['Parameters'][1]
    hold = expr('VirtualFunction', VirtualFunctionName='SetManualCameraFade',
                Parameters=[expr('FloatConst', Value=1.0), color, expr('False')])
    cards_start = len(graph)
    sequence = graph[109]['VariableExpression']
    cards = graph[108]['VariableExpression']
    graph.extend([jump_if_missing(cards, 1391), jump_if_missing(sequence, 1391), *graph[85:89],
                  {**graph[79], 'ObjectExpression': camera_manager, 'Offset': 29, 'ContextExpression': hold},
                  expr('Jump', CodeOffset=1391)])

    # With both title sequences present, defer the game's own fade-in until
    # the camera starts and its menu is registered. Other modes keep their
    # original cards-finished fade and camera-ready menu registration.
    cards_finished = len(graph)
    graph.extend([jump_if_missing(cards, 1760), jump_if_missing(sequence, 1760), graph[3]])
    ready = len(graph)
    graph.extend([jump_if_missing(cards, 1705), jump_if_missing(sequence, 1705),
                  {**graph[0], 'PushingAddress': 1760}, expr('Jump', CodeOffset=188), end])
    return begin, [(graph[110], 'EX_Jump', 'CodeOffset', 26, begin),
                   (graph[116], 'EX_Jump', 'CodeOffset', 1391, cards_start),
                   (graph[100], 'EX_Jump', 'CodeOffset', 1760, cards_finished),
                   (graph[118], 'EX_Jump', 'CodeOffset', 1705, ready)]


def build(args):
    game = args.game_root
    if sha(args.helper) != args.helper_sha256:
        raise ValueError('Unexpected loose-only compatibility DLL hash')
    args.output.mkdir(parents=True, exist_ok=False)
    stage = args.output / 'stage'
    def tool(action, path, out):
        run('dotnet', args.asset_tool, action, path, out)

    def read(file):
        return json.loads(file.read_text(encoding='utf-8-sig'))

    def original(relative):
        path = game / relative
        for p in (path, path.with_suffix('.uexp')):
            if sha(p) != SOURCE_HASHES[p.relative_to(game).as_posix()]:
                raise ValueError(f'Original differs: {p}')
        out = args.output / (path.stem + '.json')
        tool('dump', path, out)
        return read(out)

    def write(asset, relative):
        path = stage / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = args.output / 'edit.json'
        temp.write_text(json.dumps(asset, ensure_ascii=False), encoding='utf-8')
        tool('write', temp, path)
        tool('dump', path, temp)
        result = read(temp)
        for before, after in zip(asset['Exports'], result['Exports'], strict=True):
            for key in ('ObjectName', 'ClassIndex', 'TemplateIndex', 'Data', 'ScriptBytecode',
                        'CreateBeforeSerializationDependencies', 'CreateBeforeCreateDependencies',
                        'SerializationBeforeCreateDependencies'):
                if before.get(key) != after.get(key):
                    raise ValueError(f'{relative}: round-trip changed {before["ObjectName"]}.{key}')
        if asset['Imports'] != result['Imports']:
            raise ValueError(f'{relative}: serialized imports differ')
        return path

    def offsets(path):
        out = args.output / 'offsets.json'
        tool('offsets', path, out)
        return read(out)[GRAPH]

    cards = original(CARDS)
    short_cards(cards)
    write(cards, SHORT)
    title = original(TITLE_WIDGET)
    show_title_opaque(title)
    write(title, TITLE_WIDGET)
    level = original(MENU)
    initial = offsets(game / MENU)
    start, links = menu(level)
    positions = offsets(write(level, MENU))
    if positions[:start] != initial[:-1]:
        raise ValueError('Original bytecode addresses moved')
    for node, kind, key, old, target in links:
        replace(node, kind, key, old, positions[target])
    if offsets(write(level, MENU)) != positions:
        raise ValueError('Final title bytecode addresses changed')
    if any((game / name).exists() for name in (CONFIG, SHORT, Path(SHORT).with_suffix('.uexp'))):
        raise ValueError('Unexpected private sequence or WindowsGame.ini in stock install')
    config = stage / CONFIG
    config.parent.mkdir(parents=True)
    config.write_bytes(b'[/Script/MoviePlayer.MoviePlayerSettings]\r\n-StartupMovies=SplashMovie_1\r\n')
    members = [CONFIG] + [p for rel in (SHORT, MENU, TITLE_WIDGET)
                          for p in (rel, Path(rel).with_suffix('.uexp').as_posix())]
    pak = pack_and_check(args, stage, members, stage / 'Tchia')
    main = write_archive(args.output / f'Tchia-IntroSkip-{args.build_id}.zip',
                         {DLL: args.helper, PAK: pak})
    optional = write_archive(args.output / f'Tchia-IntroSkip-Loose-Optional-{args.build_id}.zip',
                             {member: stage / member for member in members})
    print(json.dumps({**main, 'optional_loose': optional,
                      'pak_members': members, 'live_verified': False}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('game-root', 'asset-tool', 'unreal-pak', 'helper', 'output'):
        parser.add_argument('--'+name, type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument('--helper-sha256', default=DLL_HASH,
                        help='SHA-256 of the compatibility DLL when built locally')
    parser.add_argument('--build-id', required=True)
    build(parser.parse_args())
