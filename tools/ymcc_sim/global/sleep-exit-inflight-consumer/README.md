# Actual nonblocking exit in-flight consumer

Reuses seven exact current marker/cleanup definitions and the actual current global declaration. Two temporal scenarios pause one real owned writer while another owned caller attempts nonblocking cleanup. All threads join; source failures must compile and fail fixed semantic checks. Type observation does not decide behavioral PASS. Positive source receives no private global/function repair.

This is distinct from17 serial marker scenarios and27 startup/recovery cases. It does not validate blocking waiters, shutdown cancellation races, direct concurrent marker helpers, dynamic race detectors, user threads, actual sleep, production Host or devices.
