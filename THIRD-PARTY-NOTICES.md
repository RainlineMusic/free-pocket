# Third-party notices

Free Pocket does not vendor any third-party source. The following components
are fetched at build time or used only for validation.

## JUCE

JUCE 8 is used as the plugin framework and is downloaded by CI from the
official JUCE releases. JUCE is dual licensed: the AGPLv3 and the commercial
JUCE license. Shipping Free Pocket as a closed-source product requires a valid
commercial JUCE license.
https://juce.com/get-juce

## AAX SDK

The AAX target is built against the AAX SDK bundled with JUCE. Distribution of
AAX plug-ins requires an Avid developer agreement and Avid code signing (PACE).
https://developer.avid.com

## VST3 SDK

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe
and other countries. The VST3 target is built through JUCE against the
Steinberg VST3 SDK and is subject to the Steinberg VST3 license.
https://www.steinberg.net/developers/

## pluginval

pluginval (Tracktion) is used in CI for validation only and is not distributed
with the product. GPLv3.
https://github.com/Tracktion/pluginval
