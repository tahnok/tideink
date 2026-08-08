"""Extra PlatformIO targets for the host renderer.

    pio run -e sim -t exec         render.png from the default fixtures
    pio run -e sim -t screenshots  regenerate every image in docs/screenshots/
"""

Import("env")  # noqa: F821  (injected by PlatformIO)

PROGRAM = "$BUILD_DIR/${PROGNAME}"

env.AddCustomTarget(
    name="exec",
    dependencies=PROGRAM,
    actions=[f"{PROGRAM} --out render.png"],
    title="Render",
    description="Render the default fixtures to render.png",
    always_build=True,
)

env.AddCustomTarget(
    name="screenshots",
    dependencies=PROGRAM,
    actions=[f"$PYTHONEXE tools/screenshots.py --binary {PROGRAM}"],
    title="Screenshots",
    description="Regenerate docs/screenshots/*.png",
    always_build=True,
)
