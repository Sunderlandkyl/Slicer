/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

/**
 * @class   vtkMRMLLabelsWidgetRepresentation2D
 * @brief   2D representation for label text with connecting lines in slice views
 *
 * This class provides 2D-specific rendering of labels in slice views.
 *
 * @sa
 * vtkMRMLLabelsWidgetRepresentation vtkMRMLLabelsWidget
 */

#ifndef __vtkMRMLLabelsWidgetRepresentation2D_h
#define __vtkMRMLLabelsWidgetRepresentation2D_h

#include "vtkMRMLLabelsWidgetRepresentation.h"
#include "vtkMRMLDisplayableManagerExport.h"

class vtkMRMLSliceNode;

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLLabelsWidgetRepresentation2D
  : public vtkMRMLLabelsWidgetRepresentation
{
public:
  static vtkMRMLLabelsWidgetRepresentation2D* New();
  vtkTypeMacro(vtkMRMLLabelsWidgetRepresentation2D, vtkMRMLLabelsWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Set the slice node for coordinate conversions
  virtual void SetSliceNode(vtkMRMLSliceNode* sliceNode);
  vtkMRMLSliceNode* GetSliceNode();

  /// Update label positions (implements collision avoidance)
  void UpdateLabelPositions() override;

protected:
  vtkMRMLLabelsWidgetRepresentation2D();
  ~vtkMRMLLabelsWidgetRepresentation2D() override;

  /// Convert world coordinates to display coordinates
  void WorldToDisplay(const double worldPos[3], double displayPos[2]) override;

  vtkWeakPointer<vtkMRMLSliceNode> SliceNode;

private:
  vtkMRMLLabelsWidgetRepresentation2D(const vtkMRMLLabelsWidgetRepresentation2D&) = delete;
  void operator=(const vtkMRMLLabelsWidgetRepresentation2D&) = delete;
};

#endif
