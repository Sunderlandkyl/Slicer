/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

/**
 * @class   vtkMRMLFocusRepresentation
 * @brief   TODO
 *
 * @sa TODO
 * vtkSliceIntersectionWidget vtkWidgetRepresentation vtkAbstractWidget
*/

#ifndef vtkMRMLFocusRepresentation_h
#define vtkMRMLFocusRepresentation_h

#include "vtkMRMLDisplayableManagerExport.h" // For export macro
#include "vtkMRMLAbstractWidgetRepresentation.h"

#include "vtkMRMLSelectionNode.h"

class SliceIntersectionInteractionDisplayPipeline;
class vtkMRMLInteractionEventData;

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLFocusRepresentation : public vtkMRMLAbstractWidgetRepresentation
{
  public:
    /**
     * Instantiate this class.
     */
    static vtkMRMLFocusRepresentation* New();

    //@{
    /**
     * Standard methods for instances of this class.
     */
    vtkTypeMacro(vtkMRMLFocusRepresentation, vtkMRMLAbstractWidgetRepresentation);
    void PrintSelf(ostream& os, vtkIndent indent) override;
    //@}

    bool IsDisplayable();
    void UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void* callData = nullptr) override;

    //@{
    /**
     * Methods to make this class behave as a vtkProp.
     */
    void GetActors2D(vtkPropCollection*) override;
    void ReleaseGraphicsResources(vtkWindow*) override;
    int RenderOverlay(vtkViewport* viewport) override;
    //@}

    /// Return found component type (as vtkMRMLInteractionDisplayNode::ComponentType).
    /// closestDistance2 is the squared distance in display coordinates from the closest position where interaction is possible.
    /// componentIndex returns index of the found component (e.g., if control point is found then control point index is returned).
    virtual std::string CanInteract(vtkMRMLInteractionEventData* interactionEventData,
        int& foundComponentType, int& foundComponentIndex, double& closestDistance2, double& handleOpacity);


    /// Get the list of info for all interaction handles
    typedef std::vector<HandleInfo> HandleInfoList;
    virtual HandleInfoList GetHandleInfoList(SliceIntersectionInteractionDisplayPipeline* pipeline);

    virtual int GetTranslateArrowCursor(const std::string& intersectingSliceNodeID);

  protected:
    vtkMRMLFocusRepresentation();
    ~vtkMRMLFocusRepresentation() override;

    class vtkInternal;
    vtkInternal* Internal;

  private:
    vtkMRMLFocusRepresentation(const vtkMRMLFocusRepresentation&) = delete;
    void operator=(const vtkMRMLFocusRepresentation&) = delete;
};

#endif
