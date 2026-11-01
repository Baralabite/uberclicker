import { test } from "node:test";
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";

test("compiled schema validates without runtime code generation", () => {
  const result = spawnSync(process.execPath, ["--disallow-code-generation-from-strings", "--input-type=module"], {
    input: `
      import {readFileSync} from 'node:fs';
      import assert from 'node:assert/strict';
      const source = readFileSync('src/generated/config-validator.ts', 'utf8');
      const {default: validate} = await import('data:text/javascript,' + encodeURIComponent(source));
      const fixture = JSON.parse(readFileSync('../contracts/fixtures/config-default.json', 'utf8'));
      assert.equal(validate(fixture), true);
      fixture.profiles[0].name = '🦘'.repeat(25);
      assert.equal(validate(fixture), false);
      fixture.profiles[0].name = 'Clicker';
      fixture.schemaVersion = 999;
      assert.equal(validate(fixture), false);
    `,
    encoding: "utf8",
  });
  assert.equal(result.status, 0, result.stderr);
});
