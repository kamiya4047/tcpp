# Privacy

Ordinary input and every bundled local provider work without a network connection. No telemetry, clipboard history, or composition logging is implemented. Smart paste defaults off. Clipboard transformations use a transient copy and never change the system clipboard during preview. The ordinary paste path remains available.

The engine suppresses contextual/English/predictive suggestions and utility providers for sensitive input. The Windows adapter checks input scope when the host exposes it; applications that do not expose a sensitive scope cannot be identified reliably. Explicit user phrases are kept in the user's local dictionary. Learning is optional and must not record sensitive fields.

Internet and neural flags alone do not establish a working provider/model. A release must state exactly which adapters and model artifacts are shipped and tested. No background service or data collection is installed by building or running the playground.
