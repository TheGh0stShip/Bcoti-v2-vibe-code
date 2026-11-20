"""Phone UI checks against tools/ui_fixture.py, never against a real device."""
from pathlib import Path
import json
from playwright.sync_api import sync_playwright, expect

OUT = Path(__file__).resolve().parent / "artifacts"
OUT.mkdir(exist_ok=True)

with sync_playwright() as p:
    browser = p.chromium.launch()
    page = browser.new_page(viewport={"width": 390, "height": 844}, is_mobile=True, has_touch=True)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto("http://127.0.0.1:8765")
    expect(page.locator("#save_status")).to_contain_text("Saved.")
    page.locator("#center").click()
    expect(page.locator("#x_value")).to_have_text("128")
    page.locator("#x_plus").click()
    expect(page.locator("#x_value")).to_have_text("129")
    page.locator("#y_minus").click()
    expect(page.locator("#y_value")).to_have_text("95")
    page.locator("#flip_mode").select_option("2")
    expect(page.locator("#zoom_x")).to_be_disabled()
    expect(page.locator("#mode_note")).to_contain_text("retained")
    page.locator("#flip_mode").select_option("0")
    expect(page.locator("#x_value")).to_have_text("129")
    expect(page.locator("#y_value")).to_have_text("95")
    page.locator("#active_profile").select_option("2")
    expect(page.locator("#save_status")).to_contain_text("Saved.")
    expect(page.locator("#x_value")).to_have_text("129")
    for _ in range(11):
        page.locator("#x_plus").click()
        expect(page.locator("#save_status")).to_contain_text("Saved.")
    expect(page.locator("#x_value")).to_have_text("140")
    expect(page.locator("#x_plus")).to_be_disabled()
    expect(page.locator("#limit_note")).to_contain_text("limit reached")
    page.screenshot(path=str(OUT / "alignment-limit-phone.png"), full_page=True)
    page.locator("#center").click()
    expect(page.locator("#x_value")).to_have_text("128")
    expect(page.locator("#y_value")).to_have_text("96")

    page.route("**/set", lambda route: route.fulfill(
        status=500, content_type="application/json",
        body='{"saved":false,"message":"SAVE FAILED"}'))
    page.locator("#x_plus").click()
    expect(page.locator("#save_status")).to_contain_text("SAVE FAILED")
    expect(page.locator("#x_value")).to_have_text("128")
    page.unroute("**/set")
    page.locator("#retry").click()
    expect(page.locator("#save_status")).to_contain_text("Saved.")
    page.route("**/set", lambda route: route.abort())
    page.locator("#x_plus").click()
    expect(page.locator("#save_status")).to_contain_text("Update not confirmed")
    page.unroute("**/set")
    page.locator("#reload").click()
    expect(page.locator("#save_status")).to_contain_text("Saved.")
    page.locator('#mark').click()
    page.locator('#x_plus').click()
    expect(page.locator('#x_value')).to_have_text('129')
    page.locator('#undo').click()
    expect(page.locator('#x_value')).to_have_text('128')
    page.locator('#x_plus').scroll_into_view_if_needed()
    expect(page.locator('#x_plus')).to_be_enabled()
    button = page.locator('#x_plus').bounding_box()
    page.mouse.move(button['x']+10,button['y']+10)
    page.mouse.down()
    page.wait_for_timeout(1450)
    page.mouse.up()
    page.wait_for_timeout(100)
    held = int(page.locator('#x_value').inner_text())
    assert 130 <= held <= 132, held
    page.wait_for_timeout(750)
    assert int(page.locator('#x_value').inner_text()) == held
    page.locator('#center').click()
    expect(page.locator('#x_value')).to_have_text('128')
    page.locator('#backup_section summary').click()
    with page.expect_download() as download:
        page.locator('#export').click()
    backup = json.loads(Path(download.value.path()).read_text())
    assert backup['settings'][4:6] == [128,96]
    page.locator('#import').set_input_files({'name':'bad.json','mimeType':'application/json','buffer':b'{"version":99}'})
    expect(page.locator('#save_status')).to_contain_text('Import rejected')
    page.on('dialog', lambda dialog: dialog.accept())
    page.locator('#x_plus').click()
    expect(page.locator('#x_value')).to_have_text('129')
    page.locator('#import').set_input_files({'name':'settings.json','mimeType':'application/json','buffer':json.dumps(backup).encode()})
    expect(page.locator('#x_value')).to_have_text('128')
    page.locator('#advanced summary').click()
    page.locator('#detail_preset').click()
    expect(page.locator('#active_profile')).to_have_value('4')
    expect(page.locator('#scene_mode')).to_have_value('3')
    expect(page.locator('#preset_en')).not_to_be_checked()
    expect(page.locator('#x_value')).to_have_text('128')
    page.locator('#contrast').focus()
    expect(page.locator('#control_help')).to_contain_text('subtle temperature')
    assert page.locator('button,input,select,summary').evaluate_all('(items)=>items.every(e=>e.title.length>10)')
    # Simulate a physical button update through the fixture, then wait for polling.
    page.locator('#contrast').blur()
    page.request.post('http://127.0.0.1:8765/set',data={'active_profile':0})
    expect(page.locator('#active_profile')).to_have_value('0',timeout=6000)
    page.locator('#help_section summary').click()
    assert page.locator('#help_list dd').count() >= 24
    for width in (320, 390, 768):
        page.set_viewport_size({"width": width, "height": 844})
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth"), width
    page.set_viewport_size({"width": 390, "height": 844})
    page.screenshot(path=str(OUT / "help-phone.png"), full_page=True)
    for section in ('advanced','backup_section','help_section'):
        if page.locator('#'+section).get_attribute('open') is not None:
            page.locator('#'+section+' summary').click()
    page.screenshot(path=str(OUT / "alignment-phone.png"), full_page=True)
    assert not errors, errors
    browser.close()
print("PASS phone controls, hold/release, checkpoint, backup/import, Detail slot, polling, help coverage, errors and 320/390/768px layouts")
