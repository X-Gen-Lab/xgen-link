# Compression and Encryption

The current implementation does not support compression or payload encryption. Authentication is a separate capability described in [Security](security.md); an authentication tag does not conceal payload bytes.

## Rejected Configuration

Enabling compression or encryption in public feature configuration is rejected with `XGL_ERR_UNSUPPORTED`. A nonzero compression selection on transmission is rejected rather than silently ignored. There is no codec registry in the protocol library.

## Wire Compatibility

Do not treat a local transformation as an interoperable wire feature. A future codec requires a defined algorithm identifier, expansion bound, authenticated metadata, receiver admission rules and bidirectional interoperability tests before it can be enabled. Reserved wire flags or extension types alone do not provide that contract.

## Application Transformations

An application can encode its own message body before submission when both endpoints agree on that application format. Its maximum encoded size must fit the configured payload or message limit, and any decoded-size limit belongs to the application. This does not enable the protocol compression/encryption configuration.
