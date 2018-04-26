/*
  ==============================================================================

    Oscilloscope.cpp
    Created: 4 Feb 2018 2:34:56pm
    Author:  Andrew

  ==============================================================================
*/

#include "Oscilloscope.h"

Oscilloscope::Background::Background (Oscilloscope* parentOscilloscope)
    :   parentScope (parentOscilloscope)
{
    setBufferedToImage (true);
}
void Oscilloscope::Background::paint (Graphics& g)
{
    parentScope->paintScale (g);
}

Oscilloscope::Foreground::Foreground (Oscilloscope* parentOscilloscope)
    :   parentScope (parentOscilloscope)
{ }
void Oscilloscope::Foreground::paint (Graphics& g)
{
    parentScope->paintWaveform (g);
}

Oscilloscope::Oscilloscope ()
    :   background (this),
        foreground (this),
        oscProcessor (nullptr)
{
    this->setOpaque (true);
    this->setPaintingIsUnclipped (true);

    addAndMakeVisible (background);
    addAndMakeVisible (foreground);

    addMouseListener (this, true);
    foreground.setMouseCursor (MouseCursor::CrosshairCursor);
}
Oscilloscope::~Oscilloscope ()
{
    if (oscProcessor != nullptr)
        oscProcessor->removeListener (this);
}
void Oscilloscope::paint (Graphics&)
{ }
void Oscilloscope::resized ()
{
    calculateRatios();
    background.setBounds (getLocalBounds());
    foreground.setBounds (getLocalBounds());
}
void Oscilloscope::mouseMove (const MouseEvent& event)
{
    currentX = event.x;
    currentY = event.y;
}
void Oscilloscope::mouseExit (const MouseEvent&)
{
    // Set to -1 to indicate out of bounds
    currentX = -1;
    currentY = -1;
}
void Oscilloscope::assignOscProcessor (OscilloscopeProcessor* oscProcessorPtr)
{
    jassert (oscProcessorPtr != nullptr);
    oscProcessor = oscProcessorPtr;
    if (maxXsamples == 0)
        maxXsamples = oscProcessor->getMaximumBlockSize();
    prepare();
    oscProcessor->addListener (this);
}
void Oscilloscope::audioProbeUpdated (AudioProbe<OscilloscopeProcessor::OscilloscopeFrame>* audioProbe)
{
    if (oscProcessor->ownsProbe (audioProbe))
    {
        repaint(); // TODO - consider decoupling repaints from frame delivery
        const ScopedLock copyLock (critSection);
        for (auto ch = 0; ch < oscProcessor->getNumChannels(); ++ch)
            oscProcessor->copyFrame (buffer.getWritePointer(ch), ch);
    }
}
void Oscilloscope::prepare()
{
    jassert (oscProcessor != nullptr); // oscProcessor should be assigned & prepared first
    buffer.setSize (oscProcessor->getNumChannels(), oscProcessor->getMaximumBlockSize());
    calculateRatios();
}
void Oscilloscope::setMaxAmplitude(const float maximumAmplitude)
{
    amplitudeMax = maximumAmplitude;
    calculateRatios();
}
float Oscilloscope::getMaxAmplitude () const
{
    return amplitudeMax;
}
void Oscilloscope::setXmin (const int minimumX)
{
    minXsamples = minimumX;
    calculateRatios();
}
int Oscilloscope::getXmin () const
{
    return minXsamples;
}
void Oscilloscope::setXmax (const int maximumX)
{
    maxXsamples = maximumX;
    calculateRatios();
}
int Oscilloscope::getXmax () const
{
    return maxXsamples;
}
void Oscilloscope::setAggregationMethod (const AggregationMethod method)
{
    aggregationMethod = method;
}
void Oscilloscope::paintWaveform (Graphics& g) const
{
    // To speed things up we make sure we stay within the graphics context so we can disable clipping at the component level

    for (auto ch = 0; ch < oscProcessor->getNumChannels(); ++ch)
    {
        auto* y = buffer.getReadPointer (ch);

        // Draw a line representing the freq data for this channel
        Path p;
        p.preallocateSpace ((getWidth() + 1) * 3);

        // Start path at first value
        auto i = minXsamples;
        const auto limit = maxXsamples - 1; // Reduce by 1 because of way while loop is structured
        auto curPx = toPxFromTime (i);
        p.startNewSubPath (curPx, toPxFromAmp (y[i]));

        // Iterate through x and plot each point, but aggregate across y if x interval is less than a pixel
        while (i < limit)
        {
            const auto nextPx = curPx + 1;
            if (aggregationMethod == AggregationMethod::average)
            {
                auto ySum = y[i];
                auto count = 1;
                while (i < limit && curPx < nextPx)
                {
                    i++;
                    curPx = toPxFromTime (i);
                    ySum += y[i];
                    count++;
                }
                i++;
                p.lineTo (curPx, toPxFromAmp (ySum / static_cast<float> (count)));
            }
            else
            {
                auto yMax = y[i];
                while (i < limit && curPx < nextPx)
                {
                    i++;
                    curPx = toPxFromTime (i);
                    if (std::abs(y[i]) > std::abs(yMax))
                        yMax = y[i];
                }
                i++;
                p.lineTo (curPx, toPxFromAmp (yMax));
            }
        }
        const auto pst = PathStrokeType (1.0f);
        g.setColour (getColourForChannel (ch));
        g.strokePath(p, pst);
    }

    // Output mouse co-ordinates in Hz/dB
    if (currentX >= 0 && currentY >= 0)
    {
        g.setColour (Colours::white);
        g.setFont (Font (GUI_SIZE_F(0.5)));
        const auto time = toTimeFromPx (static_cast<float> (currentX));
        const auto ampStr = String (toAmpFromPx (static_cast<float> (currentY)), 1);
        const auto txt =  String (time) + ", " + ampStr;
        const auto offset = GUI_GAP_I(2);
        auto lblX = currentX + offset;
        auto lblY = currentY + offset;
        const auto lblW = GUI_SIZE_I(4.1);
        const auto lblH = GUI_SIZE_I(0.6);
        auto lblJust = Justification::centredLeft;
        if (lblX + lblW > getWidth())
        {
            lblX = currentX - offset - lblW;
            lblJust = Justification::centredRight;
        }
        if (lblY + lblH > getHeight())
            lblY = currentY - offset - lblH;
        g.drawText (txt, lblX, lblY, lblW, lblH, lblJust, false);
    }
}
void Oscilloscope::paintScale (Graphics& g) const
{
    // To speed things up we make sure we stay within the graphics context so we can disable clipping at the component level

    g.setColour (Colours::black);
    g.fillRect (getLocalBounds());

    const auto axisColour = Colours::darkgrey.darker();
    const auto textColour = Colours::grey.darker();

    g.setColour (axisColour);
    g.drawRect (getLocalBounds().toFloat());

    g.setFont (Font (GUI_SIZE_I(0.4)));

    // Plot amplitude scale (just halves, quarters or eighths)
    auto maxTicks = getHeight() / GUI_SIZE_I(2);
    int numTicks;
    if (maxTicks >= 8)
        numTicks = 8;
    else if (maxTicks >= 4)
        numTicks = 4;
    else
        numTicks = 2;

    // Draw y scale for amplitude
    for (auto t = 0; t < numTicks; ++t)
    {
        const auto scaleY = static_cast<float> (getHeight()) / static_cast<float> (numTicks) * static_cast<float> (t);
        g.setColour (axisColour);
        if (t > 0)
            g.drawHorizontalLine (static_cast<int> (scaleY), 0.0f, static_cast<float> (getWidth()));
        g.setColour (textColour);
        const auto ampStr = String (toAmpFromPx (scaleY), 1);
        const auto lblX = GUI_SIZE_I(0.1);
        const auto lblY = static_cast<int> (scaleY) + GUI_SIZE_I(0.1);
        const auto lblW = GUI_SIZE_I(1.1);
        const auto lblH = static_cast<int> (scaleY) + GUI_SIZE_I(0.6);
        g.drawText (ampStr, lblX, lblY, lblW, lblH, Justification::topLeft, false);
    }
   
    // Plot time scale (in samples)
    maxTicks = getWidth() / GUI_SIZE_I(2);
    numTicks = 0;
    if (maxTicks >= 16)
        numTicks = 16;
    else if (maxTicks >= 8)
        numTicks = 8;
    else if (maxTicks >= 4)
        numTicks = 4;
    else if (maxTicks >= 2)
        numTicks = 2;
    for (auto t = 0; t < numTicks; ++t)
    {
        const auto scaleX = static_cast<float> (getWidth()) / static_cast<float> (numTicks) * static_cast<float> (t);
        g.setColour (axisColour);
        if (t > 0)
            g.drawVerticalLine (static_cast<int> (scaleX), 0.0f, static_cast<float> (getWidth()));
        g.setColour (textColour);
        const auto dBStr = String (static_cast<int> (toTimeFromPx (scaleX)));
        const auto lblX = static_cast<int> (scaleX) + GUI_SIZE_I(0.1);
        const auto lblY = getHeight() - GUI_SIZE_I(0.6);
        const auto lblW = GUI_BASE_SIZE_I;
        const auto lblH = GUI_SIZE_I(0.5);
        g.drawText (dBStr, lblX, lblY, lblW, lblH, Justification::topLeft, false);
    }
}
inline float Oscilloscope::toAmpFromPx (const float yInPixels) const
{
    return amplitudeMax - yInPixels * yRatioInv;
}
inline float Oscilloscope::toPxFromAmp(const float amplitude) const
{
    return (amplitudeMax - jlimit (-amplitudeMax, amplitudeMax, amplitude)) * yRatio;
}
inline int Oscilloscope::toTimeFromPx (const float xInPixels) const
{
    return static_cast<int> (xInPixels * xRatioInv) + minXsamples;
}
inline float Oscilloscope::toPxFromTime (const int xInSamples) const
{
    return (xInSamples - minXsamples) * xRatio;
}
Colour Oscilloscope::getColourForChannel (const int channel)
{
    switch (channel % 6)
    {
        case 0: return Colours::green;
        case 1: return Colours::yellow;
        case 2: return Colours::blue;
        case 3: return Colours::cyan;
        case 4: return Colours::orange;
        case 5: return Colours::magenta;
        default: return Colours::red;
    }
}
void Oscilloscope::calculateRatios()
{
    maxXsamples = jmin (maxXsamples, oscProcessor->getMaximumBlockSize());
    xRatio = static_cast<float> (getWidth()) / static_cast<float> (maxXsamples - minXsamples);
    xRatioInv = 1.0f / xRatio;
    yRatio = static_cast<float> (getHeight()) / (amplitudeMax * 2.0f);
    yRatioInv = 1.0f / yRatio;
}