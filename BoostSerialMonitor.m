function BoostSerialMonitor
% BoostSerialMonitor  Four-channel real-time serial waveform monitor.
%
% Expected DSP frame (one line per frame):
%   Vin,Vout,IL,Iout
% Example:
%   48.12,75.03,8.42,7.91
%
% Default serial settings: COM10, 115200 baud, 8N1, no flow control.
% Requires MATLAB R2019b or newer (serialport interface).

defaultChannelNames = ["Vin", "Vout", "IL", "Iout"];
defaultChannelUnits = ["V",   "V",    "A",  "A"];
defaultYAxisExpand = true(1, 4);
defaultYLimits = repmat([0 1], 4, 1);
yExpansionMargin = 0.05;
preferencesGroup = 'BoostSerialMonitor';
preferencesKey = 'ChannelDisplaySettings';
channelSettings = loadChannelSettings();
channelNames = channelSettings.names;
channelUnits = channelSettings.units;
defaultPort = "COM10";
defaultBaud = 115200;
maxStoredSamples = 360000; % About one hour at 100 samples/s.
plotPeriod = 0.05;         % Refresh plots at no more than 20 frames/s.
counterPeriod = 0.2;       % Refresh counters at no more than 5 frames/s.
maxPlotSamples = 2000;     % Limit graphics load for long time windows.

uiFontName = 'Microsoft YaHei UI';
plotFontName = 'Segoe UI';
windowColor = [0.945 0.957 0.973];
cardColor = [1.000 1.000 1.000];
borderColor = [0.820 0.850 0.900];
textColor = [0.180 0.220 0.290];
mutedTextColor = [0.390 0.440 0.520];
primaryButtonColor = [0.110 0.400 0.780];
dangerButtonColor = [0.820 0.270 0.250];
warningButtonColor = [0.960 0.650 0.180];
secondaryButtonColor = [0.920 0.940 0.970];

state.serial = [];
state.connected = false;
state.paused = false;
state.time = zeros(maxStoredSamples, 1);
state.values = zeros(maxStoredSamples, 4);
state.sampleCount = 0;
state.writeIndex = 1;
state.goodFrames = 0;
state.badFrames = 0;
state.clock = tic;
state.timeOffset = 0;
state.lastPlotTime = -inf;
state.lastCounterTime = -inf;
state.closing = false;
state.settingsFigure = [];
state.currentYLimits = channelSettings.yLimits;

fig = uifigure( ...
    'Name', 'BOOST Serial Waveform Monitor', ...
    'Position', [100 80 1200 800], ...
    'Color', windowColor, ...
    'CloseRequestFcn', @onClose);

root = uigridlayout(fig, [3 1]);
root.RowHeight = {92, 44, '1x'};
root.Padding = [14 14 14 14];
root.RowSpacing = 10;
root.BackgroundColor = windowColor;

toolbar = uigridlayout(root, [1 3]);
toolbar.Layout.Row = 1;
toolbar.ColumnWidth = {350, '1x', 275};
toolbar.Padding = [0 0 0 0];
toolbar.ColumnSpacing = 10;
toolbar.BackgroundColor = windowColor;

settingsPanel = uipanel(toolbar, 'Title', '串口设置', ...
    'BackgroundColor', cardColor, 'BorderColor', borderColor, ...
    'FontName', uiFontName, 'FontSize', 12, 'FontWeight', 'bold', ...
    'ForegroundColor', textColor);
settingsPanel.Layout.Column = 1;
settingsGrid = uigridlayout(settingsPanel, [1 4]);
settingsGrid.ColumnWidth = {42, '1x', 52, 92};
settingsGrid.Padding = [10 3 10 7];
settingsGrid.ColumnSpacing = 7;
settingsGrid.BackgroundColor = cardColor;

uilabel(settingsGrid, 'Text', '串口', 'HorizontalAlignment', 'right', ...
    'FontName', uiFontName, 'FontSize', 12, 'FontColor', mutedTextColor);
portDropDown = uidropdown(settingsGrid, 'Items', {char(defaultPort)}, ...
    'Value', char(defaultPort), 'FontName', uiFontName, 'FontSize', 12);
uilabel(settingsGrid, 'Text', '波特率', 'HorizontalAlignment', 'right', ...
    'FontName', uiFontName, 'FontSize', 12, 'FontColor', mutedTextColor);
baudField = uieditfield(settingsGrid, 'numeric', 'Value', defaultBaud, ...
    'Limits', [300 inf], 'RoundFractionalValues', true, ...
    'FontName', uiFontName, 'FontSize', 12);

actionsPanel = uipanel(toolbar, 'Title', '采集控制', ...
    'BackgroundColor', cardColor, 'BorderColor', borderColor, ...
    'FontName', uiFontName, 'FontSize', 12, 'FontWeight', 'bold', ...
    'ForegroundColor', textColor);
actionsPanel.Layout.Column = 2;
actionsGrid = uigridlayout(actionsPanel, [1 6]);
actionsGrid.ColumnWidth = {'1x', '1x', '1x', '1x', '1x', '1x'};
actionsGrid.Padding = [10 3 10 7];
actionsGrid.ColumnSpacing = 7;
actionsGrid.BackgroundColor = cardColor;

refreshButton = uibutton(actionsGrid, 'Text', '刷新串口', ...
    'ButtonPushedFcn', @onRefreshPorts);
connectButton = uibutton(actionsGrid, 'Text', '连接', ...
    'ButtonPushedFcn', @onConnectToggle);
pauseButton = uibutton(actionsGrid, 'Text', '暂停显示', ...
    'Enable', 'off', 'ButtonPushedFcn', @onPauseToggle);
resetYAxisButton = uibutton(actionsGrid, 'Text', '重置Y轴', ...
    'ButtonPushedFcn', @onResetYAxis);
clearButton = uibutton(actionsGrid, 'Text', '清屏', ...
    'ButtonPushedFcn', @onClear);
saveButton = uibutton(actionsGrid, 'Text', '保存 CSV', ...
    'ButtonPushedFcn', @onSave);

secondaryButtons = [refreshButton, pauseButton, resetYAxisButton, ...
    clearButton, saveButton];
set(secondaryButtons, 'BackgroundColor', secondaryButtonColor, ...
    'FontColor', textColor, 'FontName', uiFontName, 'FontSize', 12);
set(connectButton, 'BackgroundColor', primaryButtonColor, ...
    'FontColor', cardColor, 'FontName', uiFontName, ...
    'FontSize', 12, 'FontWeight', 'bold');

displayPanel = uipanel(toolbar, 'Title', '显示设置', ...
    'BackgroundColor', cardColor, 'BorderColor', borderColor, ...
    'FontName', uiFontName, 'FontSize', 12, 'FontWeight', 'bold', ...
    'ForegroundColor', textColor);
displayPanel.Layout.Column = 3;
displayGrid = uigridlayout(displayPanel, [1 3]);
displayGrid.ColumnWidth = {72, 70, '1x'};
displayGrid.Padding = [10 3 10 7];
displayGrid.ColumnSpacing = 7;
displayGrid.BackgroundColor = cardColor;
uilabel(displayGrid, 'Text', '窗口 (s)', 'HorizontalAlignment', 'right', ...
    'FontName', uiFontName, 'FontSize', 12, 'FontColor', mutedTextColor);
windowField = uieditfield(displayGrid, 'numeric', 'Value', 10, ...
    'Limits', [0.5 3600], 'FontName', uiFontName, 'FontSize', 12);
uibutton(displayGrid, 'Text', '通道设置', ...
    'ButtonPushedFcn', @onOpenChannelSettings, ...
    'BackgroundColor', secondaryButtonColor, 'FontColor', textColor, ...
    'FontName', uiFontName, 'FontSize', 12);

statusPanel = uipanel(root, 'BackgroundColor', cardColor, ...
    'BorderColor', borderColor);
statusPanel.Layout.Row = 2;
statusGrid = uigridlayout(statusPanel, [1 5]);
statusGrid.ColumnWidth = {28, '1x', 135, 135, 175};
statusGrid.Padding = [10 3 10 3];
statusGrid.ColumnSpacing = 8;
statusGrid.BackgroundColor = cardColor;
statusLamp = uilamp(statusGrid, 'Color', [0.65 0.65 0.65]);
statusLabel = uilabel(statusGrid, 'Text', '未连接', ...
    'FontName', uiFontName, 'FontSize', 12, ...
    'FontWeight', 'bold', 'FontColor', mutedTextColor);
goodLabel = uilabel(statusGrid, 'Text', '有效帧: 0', ...
    'HorizontalAlignment', 'right', 'FontName', uiFontName, ...
    'FontSize', 12, 'FontColor', [0.180 0.560 0.360]);
badLabel = uilabel(statusGrid, 'Text', '异常帧: 0', ...
    'HorizontalAlignment', 'right', 'FontName', uiFontName, ...
    'FontSize', 12, 'FontColor', [0.760 0.280 0.260]);
sampleLabel = uilabel(statusGrid, 'Text', '缓存点数: 0', ...
    'HorizontalAlignment', 'right', 'FontName', uiFontName, ...
    'FontSize', 12, 'FontColor', mutedTextColor);

plots = uigridlayout(root, [2 2]);
plots.Layout.Row = 3;
plots.RowHeight = {'1x', '1x'};
plots.ColumnWidth = {'1x', '1x'};
plots.Padding = [0 0 0 0];
plots.RowSpacing = 10;
plots.ColumnSpacing = 10;
plots.BackgroundColor = windowColor;

axesHandles = gobjects(4, 1);
lineHandles = gobjects(4, 1);
lineColors = [0.100 0.430 0.820; ...
              0.920 0.430 0.160; ...
              0.180 0.620 0.400; ...
              0.540 0.330 0.760];

for k = 1:4
    ax = uiaxes(plots);
    ax.Layout.Row = ceil(k / 2);
    ax.Layout.Column = mod(k - 1, 2) + 1;
    ax.Box = 'on';
    ax.Color = cardColor;
    ax.FontName = plotFontName;
    ax.FontSize = 10;
    ax.XColor = mutedTextColor;
    ax.YColor = mutedTextColor;
    ax.LineWidth = 0.8;
    ax.XGrid = 'on';
    ax.YGrid = 'on';
    ax.GridColor = borderColor;
    ax.GridAlpha = 0.28;
    title(ax, channelNames(k));
    xlabel(ax, 'Time (s)');
    ylabel(ax, channelNames(k) + " (" + channelUnits(k) + ")");
    ax.Title.FontName = plotFontName;
    ax.Title.FontSize = 14;
    ax.Title.FontWeight = 'bold';
    ax.Title.Color = textColor;
    ax.XLabel.FontName = plotFontName;
    ax.XLabel.FontSize = 11;
    ax.XLabel.Color = mutedTextColor;
    ax.YLabel.FontName = plotFontName;
    ax.YLabel.FontSize = 11;
    ax.YLabel.Color = mutedTextColor;
    lineHandles(k) = plot(ax, NaN, NaN, 'LineWidth', 1.5, ...
        'Color', lineColors(k, :));
    axesHandles(k) = ax;
end

applyChannelSettings();
onRefreshPorts();

    function onRefreshPorts(~, ~)
        if state.connected
            return;
        end

        try
            availablePorts = string(serialportlist("available"));
            allPorts = string(serialportlist("all"));
        catch
            availablePorts = strings(0, 1);
            allPorts = strings(0, 1);
        end

        if isempty(allPorts)
            portDropDown.Items = {char(defaultPort)};
            portDropDown.Value = char(defaultPort);
            statusLabel.Text = 'Windows 未检测到串口';
            return;
        end

        % Show every Windows-enumerated port. A port that is listed by
        % "all" but not by "available" is normally busy or unusable.
        portDropDown.Items = cellstr(allPorts);
        if any(strcmpi(allPorts, defaultPort))
            portDropDown.Value = char(defaultPort);
        else
            portDropDown.Value = char(allPorts(1));
        end

        if isempty(availablePorts)
            statusLabel.Text = '检测到串口，但当前没有可打开的端口';
        else
            statusLabel.Text = sprintf('可用串口: %s', ...
                strjoin(cellstr(availablePorts), ', '));
        end
    end

    function onOpenChannelSettings(~, ~)
        if ~isempty(state.settingsFigure) && isvalid(state.settingsFigure)
            return;
        end

        dialogSize = [720 335];
        mainPosition = fig.Position;
        dialogPosition = [ ...
            mainPosition(1) + (mainPosition(3) - dialogSize(1)) / 2, ...
            mainPosition(2) + (mainPosition(4) - dialogSize(2)) / 2, ...
            dialogSize];
        state.settingsFigure = uifigure( ...
            'Name', '通道与 Y 轴设置', ...
            'Position', dialogPosition, ...
            'Color', windowColor, ...
            'WindowStyle', 'modal', ...
            'CloseRequestFcn', @(~, ~) closeChannelSettings());

        dialogRoot = uigridlayout(state.settingsFigure, [3 1]);
        dialogRoot.RowHeight = {42, '1x', 48};
        dialogRoot.Padding = [16 14 16 14];
        dialogRoot.RowSpacing = 10;
        dialogRoot.BackgroundColor = windowColor;

        instructionLabel = uilabel(dialogRoot, ...
            'Text', 'Y 轴范围是最小显示范围；勾选超限扩展后，突变只会扩大范围。', ...
            'FontName', uiFontName, 'FontSize', 12, ...
            'FontColor', mutedTextColor);
        instructionLabel.Layout.Row = 1;

        rowNames = arrayfun(@(index) sprintf('Channel %d', index), ...
            1:4, 'UniformOutput', false);
        settingsTable = uitable(dialogRoot, ...
            'Data', channelSettingsToTableData(channelSettings), ...
            'ColumnName', {'名称', '单位', '超限扩展', 'Y 最小值', 'Y 最大值'}, ...
            'ColumnEditable', true(1, 5), ...
            'ColumnFormat', {'char', 'char', 'logical', 'numeric', 'numeric'}, ...
            'ColumnWidth', {170, 100, 90, 110, 110}, ...
            'RowName', rowNames, ...
            'FontName', uiFontName, 'FontSize', 12, ...
            'BackgroundColor', [cardColor; 0.975 0.980 0.990]);
        settingsTable.Layout.Row = 2;

        dialogButtons = uigridlayout(dialogRoot, [1 4]);
        dialogButtons.Layout.Row = 3;
        dialogButtons.ColumnWidth = {'1x', 120, 100, 100};
        dialogButtons.ColumnSpacing = 8;
        dialogButtons.Padding = [0 2 0 2];
        dialogButtons.BackgroundColor = windowColor;

        restoreButton = uibutton(dialogButtons, 'Text', '恢复默认值', ...
            'ButtonPushedFcn', @(~, ~) restoreDefaultSettings(settingsTable));
        restoreButton.Layout.Column = 2;
        cancelButton = uibutton(dialogButtons, 'Text', '取消', ...
            'ButtonPushedFcn', @(~, ~) closeChannelSettings());
        cancelButton.Layout.Column = 3;
        applyButton = uibutton(dialogButtons, 'Text', '应用', ...
            'ButtonPushedFcn', @(~, ~) applySettingsFromTable(settingsTable));
        applyButton.Layout.Column = 4;

        set([restoreButton, cancelButton], ...
            'BackgroundColor', secondaryButtonColor, ...
            'FontColor', textColor, 'FontName', uiFontName, 'FontSize', 12);
        set(applyButton, 'BackgroundColor', primaryButtonColor, ...
            'FontColor', cardColor, 'FontName', uiFontName, ...
            'FontSize', 12, 'FontWeight', 'bold');
    end

    function restoreDefaultSettings(settingsTable)
        settingsTable.Data = channelSettingsToTableData(defaultChannelSettings());
    end

    function applySettingsFromTable(settingsTable)
        data = settingsTable.Data;
        try
            names = strtrim(string(data(:, 1))).';
            units = strtrim(string(data(:, 2))).';
            expandY = logical(cell2mat(data(:, 3))).';
            minimums = double(cell2mat(data(:, 4)));
            maximums = double(cell2mat(data(:, 5)));
        catch
            uialert(state.settingsFigure, ...
                '请检查表格内容，上下限必须为数值。', '设置无效');
            return;
        end

        if any(strlength(names) == 0)
            uialert(state.settingsFigure, ...
                '四个通道的名称均不能为空。', '设置无效');
            return;
        end

        yLimits = [minimums(:) maximums(:)];
        if any(~isfinite(yLimits), 'all') || any(yLimits(:, 1) >= yLimits(:, 2))
            uialert(state.settingsFigure, ...
                'Y 轴上下限必须为有限数值，且最小值必须小于最大值。', ...
                '设置无效');
            return;
        end

        channelSettings.names = names;
        channelSettings.units = units;
        channelSettings.expandY = expandY;
        channelSettings.yLimits = yLimits;
        channelNames = names;
        channelUnits = units;
        applyChannelSettings();

        storedSettings = channelSettings;
        storedSettings.names = cellstr(channelSettings.names);
        storedSettings.units = cellstr(channelSettings.units);
        try
            setpref(preferencesGroup, preferencesKey, storedSettings);
        catch exception
            uialert(state.settingsFigure, ...
                ['设置已应用，但无法保存到 MATLAB 首选项。' newline ...
                exception.message], '保存设置失败');
            return;
        end

        statusLabel.Text = '通道显示设置已更新';
        closeChannelSettings();
    end

    function closeChannelSettings()
        if ~isempty(state.settingsFigure) && isvalid(state.settingsFigure)
            delete(state.settingsFigure);
        end
        state.settingsFigure = [];
    end

    function onConnectToggle(~, ~)
        if state.connected
            disconnectSerial('已断开');
            return;
        end

        port = string(portDropDown.Value);
        baud = round(baudField.Value);
        connectButton.Enable = 'off';
        statusLabel.Text = '正在连接...';
        drawnow;

        sp = [];
        try
            % Open with the minimum argument set first. Some USB-UART
            % drivers reject a constructor that negotiates every property
            % at once even though the basic port can be opened.
            sp = serialport(char(port), baud);
            sp.DataBits = 8;
            sp.StopBits = 1;
            sp.Parity = 'none';
            sp.FlowControl = 'none';
            sp.Timeout = 1;
            configureTerminator(sp, "LF");
            flush(sp);

            state.serial = sp;
            state.connected = true;
            state.timeOffset = latestSampleTime();
            state.clock = tic;
            state.lastPlotTime = -inf;
            configureCallback(sp, "terminator", @onSerialLine);

            connectButton.Text = '断开';
            connectButton.Enable = 'on';
            connectButton.BackgroundColor = dangerButtonColor;
            refreshButton.Enable = 'off';
            portDropDown.Enable = 'off';
            baudField.Enable = 'off';
            pauseButton.Enable = 'on';
            statusLamp.Color = [0.18 0.72 0.34];
            statusLabel.Text = sprintf('已连接 %s @ %d baud', port, baud);
        catch exception
            if ~isempty(sp)
                try
                    configureCallback(sp, "off");
                catch
                end
                clear sp
            end
            state.serial = [];
            state.connected = false;
            connectButton.Enable = 'on';
            connectButton.BackgroundColor = primaryButtonColor;
            statusLamp.Color = [0.85 0.25 0.20];
            statusLabel.Text = '连接失败';
            diagnosis = buildSerialDiagnosis(port, baud, exception);
            uialert(fig, diagnosis, '串口连接失败');
        end
    end

    function message = buildSerialDiagnosis(port, baud, exception)
        try
            allPorts = string(serialportlist("all"));
            availablePorts = string(serialportlist("available"));
        catch
            allPorts = strings(0, 1);
            availablePorts = strings(0, 1);
        end

        allText = portListText(allPorts);
        availableText = portListText(availablePorts);
        if any(strcmpi(availablePorts, port))
            conclusion = '端口被枚举为可用；请检查下方 MATLAB/驱动原始错误。';
        elseif any(strcmpi(allPorts, port))
            conclusion = ['Windows 能看到该端口，但 MATLAB 判定它不可打开。' ...
                '常见原因是驱动异常或其他进程占用。'];
        else
            conclusion = 'Windows 当前未枚举到所选端口，请重新插拔并刷新串口。';
        end

        message = sprintf([ ...
            '目标: %s @ %d baud\n' ...
            '全部端口: %s\n' ...
            '可用端口: %s\n\n' ...
            '判断: %s\n\n' ...
            'MATLAB 错误标识: %s\n' ...
            '原始错误: %s'], ...
            char(port), baud, allText, availableText, conclusion, ...
            exception.identifier, exception.message);
    end

    function output = portListText(ports)
        if isempty(ports)
            output = '无';
        else
            output = strjoin(cellstr(ports), ', ');
        end
    end

    function onSerialLine(source, ~)
        if state.closing || ~state.connected
            return;
        end

        try
            rawLine = strtrim(readline(source));
            fields = split(rawLine, ',');
            values = str2double(fields);

            if numel(values) ~= 4 || any(~isfinite(values))
                state.badFrames = state.badFrames + 1;
                updateCounters(false);
                return;
            end

            elapsed = state.timeOffset + toc(state.clock);
            sampleValues = reshape(values, 1, 4);
            state.time(state.writeIndex) = elapsed;
            state.values(state.writeIndex, :) = sampleValues;
            state.writeIndex = mod(state.writeIndex, maxStoredSamples) + 1;
            state.sampleCount = min(state.sampleCount + 1, maxStoredSamples);
            state.goodFrames = state.goodFrames + 1;
            expandYAxisForExtrema(sampleValues, sampleValues);

            updateCounters(false);
            if ~state.paused && elapsed - state.lastPlotTime >= plotPeriod
                updatePlots();
                state.lastPlotTime = elapsed;
            end
        catch exception
            state.badFrames = state.badFrames + 1;
            updateCounters(false);
            if state.connected
                statusLabel.Text = ['接收异常: ' exception.message];
            end
        end
    end

    function updatePlots()
        if state.sampleCount == 0 || ~isvalid(fig)
            return;
        end

        visibleEnd = latestSampleTime();
        visibleStart = max(0, visibleEnd - windowField.Value);
        firstLogicalIndex = findFirstVisibleSample(visibleStart);
        visibleCount = state.sampleCount - firstLogicalIndex + 1;

        if visibleCount <= maxPlotSamples
            logicalIndices = firstLogicalIndex:state.sampleCount;
        else
            logicalIndices = round(linspace( ...
                firstLogicalIndex, state.sampleCount, maxPlotSamples));
        end

        physicalIndices = logicalToPhysical(logicalIndices);
        shownTime = state.time(physicalIndices);
        shownValues = state.values(physicalIndices, :);
        for index = 1:4
            lineHandles(index).XData = shownTime;
            lineHandles(index).YData = shownValues(:, index);
            if visibleEnd > visibleStart
                axesHandles(index).XLim = [visibleStart visibleEnd];
            end
        end
        drawnow limitrate nocallbacks;
    end

    function updateCounters(forceUpdate)
        if ~isvalid(fig)
            return;
        end

        currentTime = state.timeOffset + toc(state.clock);
        if ~forceUpdate && currentTime - state.lastCounterTime < counterPeriod
            return;
        end

        goodLabel.Text = sprintf('有效帧: %d', state.goodFrames);
        badLabel.Text = sprintf('异常帧: %d', state.badFrames);
        sampleLabel.Text = sprintf('缓存点数: %d', state.sampleCount);
        state.lastCounterTime = currentTime;
    end

    function onPauseToggle(~, ~)
        state.paused = ~state.paused;
        if state.paused
            pauseButton.Text = '继续显示';
            pauseButton.BackgroundColor = warningButtonColor;
            pauseButton.FontColor = textColor;
            statusLabel.Text = '显示已暂停，数据仍在接收';
        else
            pauseButton.Text = '暂停显示';
            pauseButton.BackgroundColor = secondaryButtonColor;
            pauseButton.FontColor = textColor;
            if state.connected
                statusLabel.Text = sprintf('已连接 %s @ %d baud', ...
                    string(portDropDown.Value), round(baudField.Value));
            end
            updatePlots();
        end
    end

    function onResetYAxis(~, ~)
        resetYAxisRanges();
        statusLabel.Text = 'Y 轴已按当前可见数据重置';
    end

    function onClear(~, ~)
        state.sampleCount = 0;
        state.writeIndex = 1;
        state.goodFrames = 0;
        state.badFrames = 0;
        state.clock = tic;
        state.timeOffset = 0;
        state.lastPlotTime = -inf;
        state.lastCounterTime = -inf;

        for index = 1:4
            lineHandles(index).XData = NaN;
            lineHandles(index).YData = NaN;
            axesHandles(index).XLimMode = 'auto';
        end
        applyChannelSettings();
        updateCounters(true);
        statusLabel.Text = '缓存与统计已清空';
    end

    function onSave(~, ~)
        if state.sampleCount == 0
            uialert(fig, '当前没有可保存的数据。', '保存 CSV');
            return;
        end

        defaultName = ['BoostData_' datestr(now, 'yyyymmdd_HHMMSS') '.csv']; %#ok<TNOW1,DATST>
        [fileName, folder] = uiputfile('*.csv', '保存采样数据', defaultName);
        if isequal(fileName, 0)
            return;
        end

        [orderedTime, orderedValues] = orderedSamples();
        variableNames = buildCsvVariableNames();
        output = array2table([orderedTime orderedValues], ...
            'VariableNames', variableNames);
        try
            writetable(output, fullfile(folder, fileName));
            statusLabel.Text = sprintf('已保存 %d 个采样点', height(output));
        catch exception
            uialert(fig, exception.message, '保存失败');
        end
    end

    function firstIndex = findFirstVisibleSample(startTime)
        low = 1;
        high = state.sampleCount;
        while low < high
            middle = floor((low + high) / 2);
            physicalIndex = logicalToPhysical(middle);
            if state.time(physicalIndex) < startTime
                low = middle + 1;
            else
                high = middle;
            end
        end
        firstIndex = low;
    end

    function physicalIndices = logicalToPhysical(logicalIndices)
        if state.sampleCount < maxStoredSamples
            oldestIndex = 1;
        else
            oldestIndex = state.writeIndex;
        end
        physicalIndices = mod(oldestIndex + logicalIndices - 2, ...
            maxStoredSamples) + 1;
    end

    function latestTime = latestSampleTime()
        if state.sampleCount == 0
            latestTime = 0;
            return;
        end
        latestIndex = mod(state.writeIndex - 2, maxStoredSamples) + 1;
        latestTime = state.time(latestIndex);
    end

    function [orderedTime, orderedValues] = orderedSamples()
        physicalIndices = logicalToPhysical(1:state.sampleCount);
        orderedTime = state.time(physicalIndices);
        orderedValues = state.values(physicalIndices, :);
    end

    function settings = defaultChannelSettings()
        settings.names = defaultChannelNames;
        settings.units = defaultChannelUnits;
        settings.expandY = defaultYAxisExpand;
        settings.yLimits = defaultYLimits;
    end

    function settings = loadChannelSettings()
        settings = defaultChannelSettings();
        try
            if ~ispref(preferencesGroup, preferencesKey)
                return;
            end

            storedSettings = getpref(preferencesGroup, preferencesKey);
            if ~isfield(storedSettings, 'expandY') && ...
                    isfield(storedSettings, 'autoY')
                storedSettings.expandY = storedSettings.autoY;
            end
            if ~isValidChannelSettings(storedSettings)
                return;
            end

            settings.names = reshape(strtrim(string(storedSettings.names)), 1, 4);
            settings.units = reshape(strtrim(string(storedSettings.units)), 1, 4);
            settings.expandY = reshape(logical(storedSettings.expandY), 1, 4);
            settings.yLimits = double(storedSettings.yLimits);
        catch
            settings = defaultChannelSettings();
        end
    end

    function valid = isValidChannelSettings(settings)
        requiredFields = {'names', 'units', 'expandY', 'yLimits'};
        valid = isstruct(settings) && all(isfield(settings, requiredFields));
        if ~valid
            return;
        end

        try
            names = strtrim(string(settings.names));
            units = string(settings.units);
            expandY = double(settings.expandY);
            yLimits = double(settings.yLimits);
            valid = numel(names) == 4 && numel(units) == 4 && ...
                numel(expandY) == 4 && all(isfinite(expandY)) && ...
                isequal(size(yLimits), [4 2]) && ...
                all(isfinite(yLimits), 'all') && ...
                all(yLimits(:, 1) < yLimits(:, 2)) && ...
                all(strlength(names) > 0);
        catch
            valid = false;
        end
    end

    function data = channelSettingsToTableData(settings)
        data = cell(4, 5);
        for index = 1:4
            data{index, 1} = char(settings.names(index));
            data{index, 2} = char(settings.units(index));
            data{index, 3} = logical(settings.expandY(index));
            data{index, 4} = settings.yLimits(index, 1);
            data{index, 5} = settings.yLimits(index, 2);
        end
    end

    function applyChannelSettings()
        for index = 1:4
            axesHandles(index).Title.String = channelNames(index);
            if strlength(channelUnits(index)) == 0
                axesHandles(index).YLabel.String = channelNames(index);
            else
                axesHandles(index).YLabel.String = channelNames(index) + ...
                    " (" + channelUnits(index) + ")";
            end
        end
        resetYAxisRanges();
    end

    function resetYAxisRanges()
        state.currentYLimits = channelSettings.yLimits;
        updateYAxisLimits();
        if state.sampleCount == 0
            return;
        end

        visibleEnd = latestSampleTime();
        visibleStart = max(0, visibleEnd - windowField.Value);
        firstLogicalIndex = findFirstVisibleSample(visibleStart);
        logicalIndices = firstLogicalIndex:state.sampleCount;
        physicalIndices = logicalToPhysical(logicalIndices);
        visibleValues = state.values(physicalIndices, :);
        expandYAxisForExtrema(min(visibleValues, [], 1), ...
            max(visibleValues, [], 1));
    end

    function expandYAxisForExtrema(minimums, maximums)
        limitsChanged = false;
        for index = 1:4
            if ~channelSettings.expandY(index)
                continue;
            end

            baseSpan = diff(channelSettings.yLimits(index, :));
            margin = yExpansionMargin * baseSpan;
            if minimums(index) < state.currentYLimits(index, 1)
                state.currentYLimits(index, 1) = minimums(index) - margin;
                limitsChanged = true;
            end
            if maximums(index) > state.currentYLimits(index, 2)
                state.currentYLimits(index, 2) = maximums(index) + margin;
                limitsChanged = true;
            end
        end

        if limitsChanged
            updateYAxisLimits();
        end
    end

    function updateYAxisLimits()
        for index = 1:4
            axesHandles(index).YLim = state.currentYLimits(index, :);
        end
    end

    function variableNames = buildCsvVariableNames()
        channelVariableNames = channelNames;
        for index = 1:4
            if strlength(channelUnits(index)) > 0
                channelVariableNames(index) = channelNames(index) + ...
                    "_" + channelUnits(index);
            end
        end

        channelVariableNames = matlab.lang.makeValidName( ...
            cellstr(channelVariableNames), 'ReplacementStyle', 'underscore');
        channelVariableNames = matlab.lang.makeUniqueStrings( ...
            channelVariableNames, {'Time_s'});
        variableNames = [{'Time_s'}, channelVariableNames];
    end

    function disconnectSerial(message)
        if ~isempty(state.serial)
            try
                configureCallback(state.serial, "off");
            catch
            end
        end
        state.serial = [];
        state.connected = false;
        state.paused = false;

        if isvalid(fig)
            connectButton.Text = '连接';
            connectButton.Enable = 'on';
            connectButton.BackgroundColor = primaryButtonColor;
            refreshButton.Enable = 'on';
            portDropDown.Enable = 'on';
            baudField.Enable = 'on';
            pauseButton.Text = '暂停显示';
            pauseButton.Enable = 'off';
            pauseButton.BackgroundColor = secondaryButtonColor;
            pauseButton.FontColor = textColor;
            statusLamp.Color = [0.65 0.65 0.65];
            statusLabel.Text = message;
        end
    end

    function onClose(~, ~)
        state.closing = true;
        closeChannelSettings();
        disconnectSerial('正在关闭');
        delete(fig);
    end
end
