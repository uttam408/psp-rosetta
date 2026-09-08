PY := .venv/bin/python
PIP := .venv/bin/pip

.PHONY: setup demo test info clean

setup:
	python3 -m venv .venv
	$(PIP) install --quiet --upgrade pip
	$(PIP) install --quiet Pillow
	@echo "ok. ffmpeg on PATH: $$(command -v ffmpeg || echo 'MISSING - audio conversion will fail')"

demo:
	$(PY) tests/make_fixture.py build/_fixture/luftrauser
	$(PY) -m pipeline.cli build --game luftrauser --src build/_fixture/luftrauser
	$(PY) -m pipeline.cli info --game luftrauser

test:
	$(PY) -m pytest -q tests || $(PY) -m unittest discover -s tests -v

clean:
	rm -rf build
