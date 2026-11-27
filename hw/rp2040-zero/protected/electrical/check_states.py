"""Boolean check of the intended qualified-source and fresh-edge contract."""

from itertools import product


def source_paths(a_valid, b_valid, b_authorized):
    sys5 = "A" if a_valid else "B" if b_valid else "-"
    u3 = "A" if a_valid else "-"
    u4 = "A" if a_valid else "B" if b_valid and b_authorized else "-"
    return sys5, u3, u4


def latch_step(latched, prior_request, request, j2_present):
    if not j2_present:
        latched = False
    elif request and not prior_request:
        latched = True
    return latched, latched and request


def main():
    for a, b, allow in product((False, True), repeat=3):
        sys5, u3, u4 = source_paths(a, b, allow)
        assert u3 == ("A" if a else "-")
        assert u4 != "B" or b and allow
        assert sys5 != "B" or b and not a
        print(f"A={int(a)} B={int(b)} authorized={int(allow)}  SYS5={sys5} U3={u3} U4={u4}")
    latched, enabled = latch_step(False, False, True, True)
    assert latched and enabled
    latched, enabled = latch_step(latched, True, True, False)
    assert not latched and not enabled
    latched, enabled = latch_step(latched, True, True, True)
    assert not latched and not enabled
    latched, enabled = latch_step(latched, True, False, True)
    assert not enabled
    latched, enabled = latch_step(latched, False, True, True)
    assert latched and enabled
    _, enabled = latch_step(latched, True, False, True)
    assert not enabled
    print("PASS expired U2 edge not carried into a new attachment. GP8 low independently denies B.")
    print("Boolean intent only. Rails, propagation, raw VBUS capture and firmware states are qualification gates.")


if __name__ == "__main__":
    main()
