$(TOP_DIR)/$(OBJ_DIR)/%.o: %.cpp
	@echo "[CXX] " $<
	$(PREFIX)$(CXX) $(CFLAGS) $(CXXFLAGS) $(INCLUDES) -c $(DEPFLAGS) $< > $(DEPFILE).d
	$(PREFIX)$(CXX) $(CFLAGS) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TOP_DIR)/$(OBJ_DIR)/%.o: %.c
	@echo "[CC ] " $<
	$(PREFIX)$(CC) $(CFLAGS) $(INCLUDES) -MT $@ -MD -MP -MF $(DEPFILE).d -c -o $@ $< 

%.o: %.c
	@echo "[CC ] " $<
	$(PREFIX)$(CC) $(CFLAGS) $(INCLUDES) -MT $@ -MD -MP -MF $(DEPFILE).d -c -o $@ $<
