# Vendored: Pianist Transformer inference code

Unmodified copies of the three files needed for inference, taken from
https://github.com/yhj137/PianistTransformer at commit
`747df2d12291e37f6638b39f1b71517e579ad48c` (2026-06-26):

- `src/model/generate.py`
- `src/model/pianoformer.py`
- `src/utils/midi.py`

Licensed under the Apache License 2.0 (see `LICENSE` in this folder). Empty
`__init__.py` files were added so the folder can be packaged. The model weights
are not included; the plugin downloads `yhj137/pianist-transformer-rendering`
from Hugging Face on first use.
