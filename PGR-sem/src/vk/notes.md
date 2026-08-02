## Problems with GPU-driven rendering
1. "Mega-buffer" fragmentation when new objects are added and removed frequently, 
with defragmentation being expensive and complex. While VmaVirualBlock handles re-usage 
of freed up space, it gets fragmented, with defragmentation likely meaning 
full "Mega-buffer" rebuild, and reupload. Probably solvable by spliting the "Mega-buffer" 
into multiple N smaller buffers, with cost of N draw calls instead of 1.