The issue is confirmed with runtime logs.

At height 1514187:
- expected difficulty (gold chain): ~279
- computed difficulty: 188304

This proves LWMA is using wrong historical data.

Root cause:
Fresh sync builds cumulative_difficulty incorrectly before HF17.
LWMA window (last 31 blocks) is corrupted → produces inflated difficulty.

Bypassing a single block does not solve the issue.

Required fix:

When the node reaches height 1514178 (last pre-HF17 block),
before validating 1514179:

FORCE:
recalculate_difficulties(0, 1514178)

Then CLEAR ALL caches:
- m_difficulty_for_next_block_top_hash
- m_difficulty_for_next_block
- m_timestamps_and_difficulties_height
- m_timestamps
- m_difficulties

This must happen ONLY ONCE during sync.

Condition:
trigger only if current cumulative_difficulty at height 1512400
does not match checkpoint expected value.

Goal:
ensure LWMA window (1514148–1514178) contains correct difficulties (~200–300),
not inflated values.

After this, difficulty at 1514179+ must match gold chain
and PoW validation must pass.