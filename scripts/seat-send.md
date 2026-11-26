# Send a Gridpico message

Use `./scripts/seat-send architect 'message'` from your original Herdr pane. From a shell without an identifiable originating pane, supply `--from NAME@LOCATION`. The regular seat path uses the roster and native Codex queue. Its receipt records queue acceptance.

## Jill's local inbox

Strategist can append correspondence to the existing local inbox:

```sh
./scripts/seat-send --from strategist@dev --topic 'Routing status' jill 'Message text'
printf '%s\n' 'First line' 'Second line' |
  ./scripts/seat-send --from strategist@dev --topic 'Routing decision' --reply-needed jill
```

Use the exact recipient `jill` and a single-line topic without `|`. Omit `--reply-needed` for `No reply needed`. The helper assigns the next entry number and writes the header with the `Strategist` label and Europe/Berlin time in CET or CEST. Each multiline entry ends with a blank line followed by `------------`. Cooperating writers use an exclusive lock on the existing inbox file during numbering and append. The receipt confirms the local append. Jill reads the inbox independently.

Run `python3 scripts/tests/test-seat-send.py` for the disposable inbox and fake native queue checks.
