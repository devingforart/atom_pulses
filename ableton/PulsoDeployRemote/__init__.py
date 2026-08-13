def create_instance(c_instance):
    # Keep pure bridge helpers importable by offline tests without Live's
    # private _Framework package being present.
    from .pulso_deploy import PulsoDeployRemote
    return PulsoDeployRemote(c_instance)
