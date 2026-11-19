"""Restore same-row shared recipe fields in detailed input capture samples."""

_MARKER = 'accelSharedRecipeSchema'
_TEXTS = ('sourceSha256', 'sourceScope', 'provider', 'validationScope')
_PATHS = ('sourcePath', 'sourceBundle')
_LEGACY = 'legacyFieldOverride'


def expand_recipe_receipt(receipt):
    """Return the full projection, or None for a malformed or unknown compact receipt."""
    if not isinstance(receipt, dict):
        return None
    if _MARKER not in receipt:
        return receipt
    if type(receipt[_MARKER]) is not int or receipt[_MARKER] != 1:
        return None
    gyro, accel = receipt.get('gyro'), receipt.get('accel')
    if not isinstance(gyro, dict) or not isinstance(accel, dict):
        return None
    if type(gyro.get('id')) is not str or type(accel.get('id')) is not str:
        return None
    if any(key in accel for key in (*_TEXTS, *_PATHS, _LEGACY)):
        return None
    if (any(type(gyro.get(key)) is not str for key in _TEXTS) or
            any(key in gyro and type(gyro[key]) is not str for key in _PATHS) or
            type(gyro.get(_LEGACY)) is not bool):
        return None
    result = {key: value for key, value in receipt.items() if key != _MARKER}
    result['accel'] = {**accel, **{key: gyro[key] for key in (*_TEXTS, *_PATHS, _LEGACY) if key in gyro}}
    return result
