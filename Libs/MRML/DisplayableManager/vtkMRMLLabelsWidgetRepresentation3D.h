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
 * @class   vtkMRMLLabelsWidgetRepresentation3D
 * @brief   3D representation for label text with connecting lines in 3D views
 *
 * This class provides 3D-specific rendering of labels in 3D views.
 *
 * @sa
 * vtkMRMLLabelsWidgetRepresentation vtkMRMLLabelsWidget
 */

#ifndef __vtkMRMLLabelsWidgetRepresentation3D_h
#define __vtkMRMLLabelsWidgetRepresentation3D_h

#include "vtkMRMLLabelsWidgetRepresentation.h"
#include "vtkMRMLDisplayableManagerExport.h"

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLLabelsWidgetRepresentation3D
  : public vtkMRMLLabelsWidgetRepresentation
{
public:
  static vtkMRMLLabelsWidgetRepresentation3D* New();
  vtkTypeMacro(vtkMRMLLabelsWidgetRepresentation3D, vtkMRMLLabelsWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Update label positions (implements collision avoidance)
  void UpdateLabelPositions() override;

protected:
  vtkMRMLLabelsWidgetRepresentation3D();
  ~vtkMRMLLabelsWidgetRepresentation3D() override;

  /// Convert world coordinates to display coordinates
  void WorldToDisplay(const double worldPos[3], double displayPos[2]) override;

private:
  vtkMRMLLabelsWidgetRepresentation3D(const vtkMRMLLabelsWidgetRepresentation3D&) = delete;
  void operator=(const vtkMRMLLabelsWidgetRepresentation3D&) = delete;
};

#endif
