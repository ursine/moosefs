#!/usr/bin/env python3
"""Generate the standalone mfscli script using the Automake inliner rules."""

import pathlib
import re
import sys


source_path, output_path = map(pathlib.Path, sys.argv[1:3])
source = source_path.read_text()
pattern = re.compile(r"^(from\s+(common\.[a-z_]+)\s+import\s+\*\s*)#\s*MFS_INLINE_IMPORT\s*$", re.M)


def inline(match):
    module = match.group(2)
    module_path = source_path.parent.joinpath(*module.split("." )).with_suffix(".py")
    if not module_path.is_file():
        return match.group(0)
    contents = "\n".join(line for line in module_path.read_text().splitlines() if not line.startswith("from common"))
    return f"### {match.group(0)} - inlined below:\n### Imported inline from {module_path.relative_to(source_path.parent)}:\n{contents}\n### End of inline import from {module_path.relative_to(source_path.parent)}"


output = pattern.sub(inline, source)
output = output.replace("#!/usr/bin/env python3", "#!/usr/bin/env python3", 1)
destination = output_path
destination.write_text(output)
destination.chmod(0o755)
