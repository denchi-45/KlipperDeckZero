# KlipperDeckZero application icon

Green printer gantry, nozzle and print layers with a white directional control pad.
Generated using the built-in image_gen tool. Transparent RGBA PNG; only size conversion was performed after generation.

## Files

- klipperdeck-master.png: original 1254 x 1254 generated artwork, kept outside packaged assets.
- ../../../assets/images/klipperdeck-control.png: 256 x 256, AppStore icon.
- ../../../assets/images/klipperdeck-control_512.png: 512 x 512, larger export.
- ../../../assets/images/klipperdeck-control_100.png: 100 x 100, APPLaunch desktop entry.
- ../../../assets/images/klipperdeck-control_80.png: 80 x 80, small launcher export.

The original template images remain intact. app-builder.json and the desktop template reference the new icons.

## Size reference

Checked 2026-10-27: CardputerZero AppBuilder requires a square PNG from 128 to 512 px and recommends 256 px:
https://github.com/CardputerZero/AppBuilder/blob/main/docs/APP_BUILDER_JSON.md

100 and 80 px follow this repository's existing launcher asset sizes; they are not the AppStore recommendation.
Exports use Pillow LANCZOS resampling and retain transparency.

## Generation prompt

Use case: logo-brand. Create one finished app icon for KlipperDeckZero, a Klipper/Moonraker 3D-printer control application on a tiny handheld computer. Square 1024x1024 PNG master. A bold premium flat graphic: dark charcoal rounded-square tile with a vivid fresh green stylized 3D printer gantry and extruder nozzle above three broad stacked print layers, subtly integrate a small off-white four-way directional control pad into the lower right of the graphic to convey remote operation. Strong simple silhouette, generous separation of shapes, carefully centered balanced composition, thick shapes that remain clear at 80x80. Green consistent with a technical printer dashboard. Crisp vector-like edges, only minimal gentle depth, no photorealism, no perspective mockup. Tile occupies 92% of canvas; corners outside tile genuinely transparent. No letters, no words, no watermark, no multiple options, no surrounding presentation. Deliver the icon itself.
