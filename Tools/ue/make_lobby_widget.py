"""Creates /Game/UI/Lobby/WBP_Lobby, a Widget Blueprint child of UKartLobbyWidget (Source/.../Net/KartLobbyWidget.h).

Its designer tree is left empty on purpose: UKartLobbyWidget builds the lobby in C++ when the tree is empty,
so the asset is the place to restyle or rebuild the lobby in the designer later, and
UKartLobbyWidget::Show() picks it up automatically when it exists.

    .\\Tools\\ue\\run.ps1 make_lobby_widget.py
"""
import os

import unreal

quit_after = os.environ.get("KART_QUIT") == "1"
folder = "/Game/UI/Lobby"
name = "WBP_Lobby"
path = f"{folder}/{name}"


def make_widget():
    parent = unreal.load_class(None, "/Script/MovecadeKart.KartLobbyWidget")
    if parent is None:
        raise RuntimeError("KartLobbyWidget class not found; build the MovecadeKart module first")
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    try:
        # No default root panel: an empty tree is what tells UKartLobbyWidget to build the lobby itself.
        factory.set_editor_property("root_widget_class", None)
    except Exception:  # noqa: BLE001 - property name differs between engine versions
        pass
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = tools.create_asset(name, folder, unreal.WidgetBlueprint, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    unreal.log(f"make_lobby_widget: saved {path} (parent {parent.get_name()})")


try:
    make_widget()
except Exception as error:  # noqa: BLE001 - report and still quit a headless run
    unreal.log_error(f"make_lobby_widget: failed: {error}")
    raise
finally:
    if quit_after:
        unreal.SystemLibrary.quit_editor()
